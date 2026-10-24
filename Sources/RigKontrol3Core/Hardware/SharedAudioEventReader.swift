import CoreAudio
import Darwin
import Foundation

public enum RigKontrolControllerTransport: Equatable, Sendable {
    case directUSB
    case sharedAudio

    public var name: String {
        switch self {
        case .directUSB: CoreAudioUSBEventReader.backendName
        case .sharedAudio: "RigKontrol3 combined audio and controls"
        }
    }
}

/// A controller client of the single USB owner in the CoreAudio driver.
/// Uses the same stream/pump contract as the direct transport, including actual
/// write completion, acquisition timestamps, and join-before-close behavior.
public enum SharedAudioEventReader {
    public static func streamRigKontrol3BulkInput(
        didOpen: ((RigKontrolUSBOutputWriter) -> Void)? = nil,
        willClose: ((RigKontrolUSBOutputWriter) -> Void)? = nil,
        shouldContinue: () -> Bool,
        packetHandler: (RigKontrolUSBReadPacket, RigKontrolUSBOutputWriter) -> Void
    ) throws -> RigKontrolUSBStreamSummary {
        try CoreAudioUSBEventReader.stream(
            connectionFactory: { try SharedAudioConnection(endpoint: NativeSharedAudioEndpoint()) },
            didOpen: didOpen, willClose: willClose,
            shouldContinue: shouldContinue, packetHandler: packetHandler)
    }
}

protocol SharedAudioEndpoint: AnyObject, Sendable {
    func read(_ qualifier: [String: Any]) throws -> [String: Any]
    func enqueue(_ request: [String: Any]) throws
}

struct SharedAudioIdentity: Equatable {
    let boot: String
    let epoch: UInt64
}

struct SharedAudioReply {
    struct Packet {
        let sequence: UInt64
        let time: UInt64
        let bytes: [UInt8]
    }
    struct Completion {
        let client: String
        let id: UInt64
        let epoch: UInt64
        let status: Int32
        let actual: Int
    }
    let identity: SharedAudioIdentity
    let state: String
    let error: Int32
    let oldest: UInt64
    let newest: UInt64
    let packets: [Packet]
    let completions: [Completion]

    init(_ dictionary: [String: Any]) throws {
        guard Self.uint(dictionary["version"]) == 1,
              let boot = dictionary["boot"] as? String, UUID(uuidString: boot) != nil,
              let epoch = Self.uint(dictionary["epoch"]),
              let state = dictionary["state"] as? String,
              ["waiting", "ready", "failed", "gap", "stale"].contains(state),
              let error = Self.int32(dictionary["error"]),
              let oldest = Self.uint(dictionary["oldest"]),
              let newest = Self.uint(dictionary["newest"]), oldest <= newest || newest == 0,
              let rawPackets = dictionary["packets"] as? [[String: Any]], rawPackets.count <= 64,
              let rawCompletions = dictionary["completions"] as? [[String: Any]], rawCompletions.count <= 64
        else { throw SharedAudioError.invalidReply }
        identity = SharedAudioIdentity(boot: boot, epoch: epoch)
        self.state = state; self.error = error; self.oldest = oldest; self.newest = newest
        packets = try rawPackets.map {
            guard let sequence = Self.uint($0["sequence"]), sequence > 0, sequence <= newest,
                  let time = Self.uint($0["time"]), time > 0,
                  let data = $0["bytes"] as? Data, !data.isEmpty, data.count <= 512
            else { throw SharedAudioError.invalidReply }
            return Packet(sequence: sequence, time: time, bytes: Array(data))
        }
        completions = try rawCompletions.map {
            guard let client = $0["client"] as? String, UUID(uuidString: client) != nil,
                  let id = Self.uint($0["id"]), id > 0,
                  let epoch = Self.uint($0["epoch"]),
                  let status = Self.int32($0["status"]),
                  let actual = Self.uint($0["actual"]), actual <= 64
            else { throw SharedAudioError.invalidReply }
            return Completion(client: client, id: id, epoch: epoch, status: status, actual: Int(actual))
        }
    }

    static func uint(_ value: Any?) -> UInt64? {
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID(),
              number.doubleValue.isFinite, number.doubleValue >= 0,
              number.doubleValue < 18_446_744_073_709_551_616,
              number.doubleValue.rounded(.down) == number.doubleValue
        else { return nil }
        return number.uint64Value
    }

    private static func int32(_ value: Any?) -> Int32? {
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID(),
              number.doubleValue.isFinite, number.doubleValue >= Double(Int32.min),
              number.doubleValue <= Double(Int32.max),
              number.doubleValue.rounded(.towardZero) == number.doubleValue
        else { return nil }
        return number.int32Value
    }
}

enum SharedAudioError: Error, CustomStringConvertible {
    case invalidReply
    case status(OSStatus)
    var description: String {
        switch self {
        case .invalidReply: "Invalid combined audio driver reply"
        case .status(let status): "CoreAudio property error \(status)"
        }
    }
}

/// Only the pump calls readPacket; the stream owner calls write. The condition
/// carries completions across those two threads without another property reader.
final class SharedAudioConnection: CoreAudioUSBConnection, @unchecked Sendable {
    private let endpoint: any SharedAudioEndpoint
    private let identity: SharedAudioIdentity
    private let client = UUID().uuidString
    private let condition = NSCondition()
    private var cursor: UInt64
    private var nextID: UInt64 = 0
    private var pending: Set<UInt64> = []
    private var completions: [UInt64: SharedAudioReply.Completion] = [:]
    private var buffered: [RigKontrolUSBReadPacket] = []
    private var failure: String?
    private var closed = false

    init(endpoint: any SharedAudioEndpoint) throws {
        self.endpoint = endpoint
        let reply = try SharedAudioReply(endpoint.read(["version": 1]))
        if reply.state == "waiting" { throw RigKontrolUSBReadError.deviceNotFound }
        guard reply.state == "ready", reply.identity.epoch > 0 else {
            throw RigKontrolUSBReadError.operationFailed(Self.ownerFailure(reply))
        }
        identity = reply.identity
        // Historical reports cannot become presses on a new attachment. The
        // session will request a new switch snapshot after its pump is running.
        cursor = reply.newest
    }

    func readPacket() -> RigKontrolUSBInputReadResult {
        condition.lock()
        if let failure { condition.unlock(); return .failed(failure) }
        if closed { condition.unlock(); return .failed("IOKIT_NO_DEVICE (shared connection closed)") }
        if !buffered.isEmpty {
            let packet = buffered.removeFirst(); condition.unlock(); return .packet(packet)
        }
        condition.unlock()
        do {
            let reply = try SharedAudioReply(endpoint.read([
                "version": 1, "boot": identity.boot, "epoch": identity.epoch, "after": cursor,
            ]))
            guard reply.identity == identity else {
                return fail("IOKIT_NO_DEVICE (shared owner changed; fresh snapshot required)")
            }
            if reply.state == "failed" { return fail(Self.ownerFailure(reply)) }
            guard reply.state == "ready" else {
                return fail("IOKIT_NO_DEVICE (shared owner \(reply.state); fresh snapshot required)")
            }
            var packets: [RigKontrolUSBReadPacket] = []
            var expected = cursor
            for packet in reply.packets {
                guard expected != UInt64.max, packet.sequence == expected + 1 else {
                    return fail("IOKIT_NO_DEVICE (shared input sequence gap; fresh snapshot required)")
                }
                expected = packet.sequence
                packets.append(RigKontrolUSBReadPacket(
                    endpointAddress: 0x81, timestamp: Self.acquisitionDate(packet.time), bytes: packet.bytes))
            }
            condition.lock()
            cursor = expected
            for completion in reply.completions where completion.client == client
                && completion.epoch == identity.epoch && pending.contains(completion.id) {
                completions[completion.id] = completion
            }
            buffered.append(contentsOf: packets)
            condition.broadcast()
            let packet = buffered.isEmpty ? nil : buffered.removeFirst()
            condition.unlock()
            if let packet { return .packet(packet) }
            // Poll only on the background pump, including while an OUT waits.
            Thread.sleep(forTimeInterval: 0.005)
            return .idle
        } catch { return fail("IOKIT_ERROR (shared input: \(error))") }
    }

    func write(_ bytes: [UInt8], timeoutMilliseconds: UInt32) -> RigKontrolUSBWriteResult {
        guard Self.allowed(bytes) else { return .init(status: "IOKIT_INVALID_ARGUMENT", transferred: 0) }
        let timeout = min(200, max(1, timeoutMilliseconds))
        condition.lock()
        if closed || failure != nil {
            let status = failure ?? "IOKIT_NO_DEVICE (shared connection closed)"
            condition.unlock(); return .init(status: status, transferred: 0)
        }
        nextID &+= 1
        let id = nextID
        pending.insert(id)
        condition.unlock()
        do {
            try endpoint.enqueue([
                "version": 1, "boot": identity.boot, "epoch": identity.epoch,
                "client": client, "id": id, "timeoutMS": timeout, "bytes": Data(bytes),
            ])
        } catch {
            condition.lock(); pending.remove(id); condition.unlock()
            return .init(status: "IOKIT_ERROR (shared enqueue: \(error))", transferred: 0)
        }
        let deadline = Date().addingTimeInterval(Double(timeout) / 1000 + 0.025)
        condition.lock()
        defer { pending.remove(id); completions.removeValue(forKey: id); condition.unlock() }
        while !closed && failure == nil && completions[id] == nil {
            if !condition.wait(until: deadline) { break }
        }
        if let completion = completions[id] {
            let success = completion.status == 0 && completion.actual == bytes.count
            return .init(
                status: success ? "IOKIT_SUCCESS" : "IOKIT_ERROR (shared write \(completion.status), \(completion.actual)/\(bytes.count))",
                transferred: completion.actual)
        }
        failure = failure ?? "IOKIT_TIMEOUT (shared USB completion missing; retry with fresh snapshot)"
        condition.broadcast()
        return .init(status: failure!, transferred: 0)
    }

    func close() {
        condition.lock(); closed = true; buffered.removeAll(); condition.broadcast(); condition.unlock()
    }

    private func fail(_ error: String) -> RigKontrolUSBInputReadResult {
        condition.lock(); failure = error; condition.broadcast(); condition.unlock()
        return .failed(error)
    }

    static func allowed(_ bytes: [UInt8]) -> Bool {
        bytes == [0x04] || bytes == [0x0b, 1, 10, 0] || (bytes.count == 64 && bytes[0] == 0x05)
    }

    private static func ownerFailure(_ reply: SharedAudioReply) -> String {
        "IOKIT_ERROR (shared owner \(reply.state), native \(String(format: "0x%08X", UInt32(bitPattern: reply.error))))"
    }

    private static func acquisitionDate(_ ticks: UInt64) -> Date {
        var timebase = mach_timebase_info_data_t()
        mach_timebase_info(&timebase)
        let now = mach_absolute_time()
        let age = now >= ticks ? Double(now - ticks) * Double(timebase.numer) / Double(timebase.denom) / 1e9 : 0
        return Date().addingTimeInterval(-age)
    }
}

private final class NativeSharedAudioEndpoint: SharedAudioEndpoint, @unchecked Sendable {
    private let device: AudioObjectID
    private static let inputSelector: AudioObjectPropertySelector = 0x726b3369 // rk3i
    private static let outputSelector: AudioObjectPropertySelector = 0x726b336f // rk3o

    init() throws {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices, mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain)
        var size: UInt32 = 0
        var status = AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &size)
        guard status == 0 else { throw SharedAudioError.status(status) }
        var devices = [AudioObjectID](repeating: 0, count: Int(size) / MemoryLayout<AudioObjectID>.stride)
        guard !devices.isEmpty else { throw RigKontrolUSBReadError.deviceNotFound }
        status = devices.withUnsafeMutableBytes {
            AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &size, $0.baseAddress!)
        }
        guard status == 0 else { throw SharedAudioError.status(status) }
        let matching = devices.filter { device in
            var property = Self.address(Self.inputSelector)
            guard AudioObjectHasProperty(device, &property) else { return false }
            var uidAddress = Self.address(kAudioDevicePropertyDeviceUID)
            var uid: Unmanaged<CFString>?
            var uidSize = UInt32(MemoryLayout<Unmanaged<CFString>?>.stride)
            let result = AudioObjectGetPropertyData(device, &uidAddress, 0, nil, &uidSize, &uid)
            guard result == 0, uidSize == MemoryLayout<Unmanaged<CFString>?>.stride else {
                uid?.release(); return false
            }
            return uid?.takeRetainedValue() as String? == "com.zukomgwili.rigkontrol3.audio.device"
        }
        guard matching.count == 1, let device = matching.first else {
            throw RigKontrolUSBReadError.operationFailed(
                "IOKIT_NO_DEVICE (combined RK3 audio driver unavailable\(matching.count > 1 ? "; multiple owners published" : ""))")
        }
        self.device = device
    }

    func read(_ qualifier: [String: Any]) throws -> [String: Any] {
        var address = Self.address(Self.inputSelector)
        var query: CFPropertyList = qualifier as CFDictionary
        var result: Unmanaged<CFPropertyList>?
        var size = UInt32(MemoryLayout<Unmanaged<CFPropertyList>?>.stride)
        let status = withUnsafePointer(to: &query) {
            AudioObjectGetPropertyData(device, &address, UInt32(MemoryLayout<CFPropertyList>.stride), $0, &size, &result)
        }
        guard status == 0 else { result?.release(); throw SharedAudioError.status(status) }
        guard size == MemoryLayout<Unmanaged<CFPropertyList>?>.stride else {
            result?.release(); throw SharedAudioError.invalidReply
        }
        guard let value = result?.takeRetainedValue() as? [String: Any] else { throw SharedAudioError.invalidReply }
        return value
    }

    func enqueue(_ request: [String: Any]) throws {
        var address = Self.address(Self.outputSelector)
        var value: CFPropertyList = request as CFDictionary
        let status = withUnsafePointer(to: &value) {
            AudioObjectSetPropertyData(device, &address, 0, nil, UInt32(MemoryLayout<CFPropertyList>.stride), $0)
        }
        guard status == 0 else { throw SharedAudioError.status(status) }
    }

    private static func address(_ selector: AudioObjectPropertySelector) -> AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(mSelector: selector, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
    }
}
