import CRK3CoreAudioUSB
import Foundation
import IOKit

/// The native connection is opened, written and closed by the stream owner.
/// Only its bounded IN read runs on the input pump; close follows the joined read.
protocol CoreAudioUSBConnection: AnyObject {
    func readPacket() -> RigKontrolUSBInputReadResult
    func write(_ bytes: [UInt8], timeoutMilliseconds: UInt32) -> RigKontrolUSBWriteResult
    func close()
}

/// Controller-only adaptation of rk3-coreaudio's IOUSBLib transport. It does not
/// create audio streams, MIDI ports, a relay socket, or a libusb context.
public enum CoreAudioUSBEventReader {
    public static let backendName = "rk3-coreaudio IOKit"
    public static let upstreamRevision = "d0679238ad06842f382ef89b995823e978caec9b"

    public static func streamRigKontrol3BulkInput(
        didOpen: ((RigKontrolUSBOutputWriter) -> Void)? = nil,
        willClose: ((RigKontrolUSBOutputWriter) -> Void)? = nil,
        shouldContinue: () -> Bool,
        packetHandler: (RigKontrolUSBReadPacket, RigKontrolUSBOutputWriter) -> Void
    ) throws -> RigKontrolUSBStreamSummary {
        try stream(
            connectionFactory: { try NativeCoreAudioUSBConnection() },
            didOpen: didOpen, willClose: willClose,
            shouldContinue: shouldContinue, packetHandler: packetHandler)
    }

    static func stream(
        connectionFactory: () throws -> any CoreAudioUSBConnection,
        didOpen: ((RigKontrolUSBOutputWriter) -> Void)? = nil,
        willClose: ((RigKontrolUSBOutputWriter) -> Void)? = nil,
        shouldContinue: () -> Bool,
        packetHandler: (RigKontrolUSBReadPacket, RigKontrolUSBOutputWriter) -> Void
    ) throws -> RigKontrolUSBStreamSummary {
        let connection = try connectionFactory()
        let pump = RigKontrolUSBInputPump { connection.readPacket() }
        defer {
            // Never close a COM interface while a bounded read still uses it.
            pump.stopAndWait()
            connection.close()
        }
        let writer = RigKontrolUSBOutputWriter(endpointAddress: 0x01) {
            connection.write($0, timeoutMilliseconds: $1)
        }
        var packetCount = 0
        var errors: [String] = []
        if let error = pump.start() {
            errors.append(error)
        } else {
            // Keep EP1-IN draining throughout startup and final LED writes.
            didOpen?(writer)
            while shouldContinue() {
                let packet = pump.nextPacket()
                if let error = pump.failure {
                    errors.append(error)
                    break
                }
                if let packet, shouldContinue() {
                    packetCount += 1
                    packetHandler(packet, writer)
                }
            }
            willClose?(writer)
        }
        // A read can fail during cleanup; keep that failure in the result.
        pump.stopAndWait()
        if let error = pump.failure, !errors.contains(error) { errors.append(error) }
        return RigKontrolUSBStreamSummary(
            openStatus: "IOKIT_SUCCESS", configurationStatus: "IOKIT_SUCCESS",
            claimStatus: "IOKIT_SUCCESS", alternateSettingStatus: "IOKIT_SUCCESS",
            releaseStatus: nil, packetCount: packetCount, nonTimeoutErrors: errors)
    }
}

/// Pointer ownership stays local; the input pump is joined before close mutates it.
private final class NativeCoreAudioUSBConnection: CoreAudioUSBConnection {
    private var usb: UnsafeMutablePointer<rk3_usb>?

    init() throws {
        let storage = UnsafeMutablePointer<rk3_usb>.allocate(capacity: 1)
        storage.initialize(to: rk3_usb())
        let status = rk3_usb_open_controls(storage)
        guard status == 0 else {
            let operation = storage.pointee.last_operation.map { String(cString: $0) } ?? "open"
            let nativeError = storage.pointee.last_error
            storage.deinitialize(count: 1)
            storage.deallocate()
            if status == RK3_ENODEVICE { throw RigKontrolUSBReadError.deviceNotFound }
            throw RigKontrolUSBReadError.operationFailed(
                "\(operation): \(Self.statusName(nativeError == 0 ? status : nativeError))")
        }
        usb = storage
    }

    deinit { close() }

    func readPacket() -> RigKontrolUSBInputReadResult {
        guard let usb else { return .failed("IOKIT_NO_DEVICE (connection closed)") }
        var buffer = [UInt8](repeating: 0, count: 512)
        var length = buffer.count
        let result = buffer.withUnsafeMutableBytes { pointer in
            rk3_bulk_read(usb, pointer.baseAddress, &length, 100)
        }
        if result == RK3_ETIMEOUT { return .idle }
        guard result == 0 else { return .failed(Self.statusName(result)) }
        guard length >= 0, length <= buffer.count else {
            return .failed("IOKIT_INVALID_READ_LENGTH (\(length))")
        }
        guard length > 0 else { return .idle }
        return .packet(RigKontrolUSBReadPacket(
            endpointAddress: 0x81, timestamp: Date(), bytes: Array(buffer.prefix(length))))
    }

    func write(_ bytes: [UInt8], timeoutMilliseconds: UInt32) -> RigKontrolUSBWriteResult {
        guard let usb else {
            return RigKontrolUSBWriteResult(status: "IOKIT_NO_DEVICE (connection closed)", transferred: 0)
        }
        guard !bytes.isEmpty else {
            return RigKontrolUSBWriteResult(status: "IOKIT_SUCCESS", transferred: 0)
        }
        // Controller writes are bounded even if an external caller requests infinity.
        let timeout = min(200, max(1, timeoutMilliseconds))
        let result = bytes.withUnsafeBytes { rk3_bulk_write(usb, $0.baseAddress, bytes.count, timeout) }
        return RigKontrolUSBWriteResult(
            status: Self.statusName(result), transferred: result == 0 ? bytes.count : 0)
    }

    func close() {
        guard let usb else { return }
        rk3_usb_close(usb)
        usb.deinitialize(count: 1)
        usb.deallocate()
        self.usb = nil
    }

    private static func statusName(_ status: Int32) -> String {
        if status == 0 { return "IOKIT_SUCCESS" }
        if status == RK3_ENODEVICE { return "IOKIT_NO_DEVICE" }
        if status == RK3_EINVAL { return "IOKIT_INVALID_ARGUMENT" }
        let code = UInt32(bitPattern: status)
        let hex = String(format: "0x%08x", code)
        switch code {
        case UInt32(truncatingIfNeeded: kIOReturnNoDevice): return "IOKIT_NO_DEVICE (\(hex))"
        case UInt32(truncatingIfNeeded: kIOReturnExclusiveAccess): return "IOKIT_EXCLUSIVE_ACCESS (\(hex))"
        case UInt32(truncatingIfNeeded: kIOReturnBusy): return "IOKIT_BUSY (\(hex))"
        default: return "IOKIT_ERROR (\(hex))"
        }
    }
}
