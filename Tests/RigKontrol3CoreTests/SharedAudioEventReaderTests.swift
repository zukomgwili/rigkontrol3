import Foundation
@testable import RigKontrol3Core
import XCTest

final class SharedAudioEventReaderTests: XCTestCase {
    func testAttachmentDropsHistoryAndPreservesNewPacketAcquisitionTime() throws {
        let endpoint = SharedEndpointFixture()
        endpoint.publish([0x04, 0x10, 0, 0, 0, 0, 0x53, 4])
        let connection = try SharedAudioConnection(endpoint: endpoint)
        endpoint.publish([0x04, 0, 0, 0, 0, 0, 0, 0])
        guard case .packet(let packet) = connection.readPacket() else {
            return XCTFail("Expected only the new snapshot, not the old held-switch edge")
        }
        XCTAssertEqual(packet.bytes, [0x04, 0, 0, 0, 0, 0, 0, 0])
        XCTAssertEqual(packet.endpointAddress, 0x81)
        XCTAssertLessThan(abs(packet.timestamp.timeIntervalSinceNow), 0.1)
        guard case .idle = connection.readPacket() else { return XCTFail("Historical edge replayed") }
        connection.close()
    }

    func testEnqueueSuccessIsNotUSBSuccessAndOnlyMatchingCompletionFinishesWrite() throws {
        let endpoint = SharedEndpointFixture()
        let connection = try SharedAudioConnection(endpoint: endpoint)
        let result = SharedWriteResult()
        let finished = DispatchSemaphore(value: 0)
        DispatchQueue.global().async {
            result.set(connection.write([0x04], timeoutMilliseconds: 200))
            finished.signal()
        }
        XCTAssertTrue(endpoint.waitForRequest())
        XCTAssertEqual(finished.wait(timeout: .now() + 0.01), .timedOut)
        endpoint.complete(actual: 1, wrongClient: true)
        _ = connection.readPacket()
        XCTAssertEqual(finished.wait(timeout: .now() + 0.01), .timedOut)
        endpoint.complete(actual: 1)
        _ = connection.readPacket()
        XCTAssertEqual(finished.wait(timeout: .now() + 0.5), .success)
        XCTAssertEqual(result.value, .init(status: "IOKIT_SUCCESS", transferred: 1))
        connection.close()
    }

    func testShortUSBWriteIsFailureEvenWhenNativeStatusIsSuccess() throws {
        let endpoint = SharedEndpointFixture()
        let connection = try SharedAudioConnection(endpoint: endpoint)
        let result = SharedWriteResult()
        let finished = DispatchSemaphore(value: 0)
        DispatchQueue.global().async {
            result.set(connection.write([0x04], timeoutMilliseconds: 200)); finished.signal()
        }
        XCTAssertTrue(endpoint.waitForRequest())
        endpoint.complete(actual: 0)
        _ = connection.readPacket()
        XCTAssertEqual(finished.wait(timeout: .now() + 0.5), .success)
        XCTAssertEqual(result.value?.transferred, 0)
        XCTAssertTrue(result.value?.status.contains("IOKIT_ERROR") == true)
    }

    func testOwnerEpochChangeFailsPendingWriterAndRequiresFreshAttachment() throws {
        let endpoint = SharedEndpointFixture()
        let connection = try SharedAudioConnection(endpoint: endpoint)
        let result = SharedWriteResult()
        let finished = DispatchSemaphore(value: 0)
        DispatchQueue.global().async {
            result.set(connection.write([0x04], timeoutMilliseconds: 200)); finished.signal()
        }
        XCTAssertTrue(endpoint.waitForRequest())
        endpoint.reconnect()
        guard case .failed(let detail) = connection.readPacket() else { return XCTFail("Old epoch accepted") }
        XCTAssertTrue(detail.contains("IOKIT_NO_DEVICE"))
        XCTAssertEqual(finished.wait(timeout: .now() + 0.5), .success)
        XCTAssertTrue(result.value?.status.contains("IOKIT_NO_DEVICE") == true)
        let fresh = try SharedAudioConnection(endpoint: endpoint)
        endpoint.publish([0x04, 0x10, 0, 0, 0, 0, 0, 0])
        guard case .packet(let packet) = fresh.readPacket() else { return XCTFail("New epoch unavailable") }
        XCTAssertEqual(packet.bytes[1], 0x10, "The session will baseline the held switch from its fresh query")
    }

    func testMissingSequenceFailsInsteadOfSynthesizingAnEdge() throws {
        let endpoint = SharedEndpointFixture()
        let connection = try SharedAudioConnection(endpoint: endpoint)
        endpoint.publish([0x04, 0x10], skippingSequence: true)
        guard case .failed(let detail) = connection.readPacket() else { return XCTFail("Gap accepted") }
        XCTAssertTrue(detail.contains("sequence gap"))
    }

    func testMissingWriteCompletionIsBoundedAndInvalidCommandsNeverReachHAL() throws {
        let endpoint = SharedEndpointFixture()
        let connection = try SharedAudioConnection(endpoint: endpoint)
        XCTAssertEqual(connection.write([0x09, 1, 2], timeoutMilliseconds: 1).status, "IOKIT_INVALID_ARGUMENT")
        XCTAssertFalse(endpoint.waitForRequest(timeout: 0.01))
        let started = ProcessInfo.processInfo.systemUptime
        let result = connection.write([0x04], timeoutMilliseconds: 10)
        XCTAssertTrue(result.status.contains("IOKIT_TIMEOUT"))
        XCTAssertLessThan(ProcessInfo.processInfo.systemUptime - started, 0.25)
    }

    func testMalformedVersionBooleanIntegerAndOversizedPacketAreRejected() throws {
        let endpoint = SharedEndpointFixture()
        var dictionary = try endpoint.read(["version": 1])
        dictionary["version"] = true
        XCTAssertThrowsError(try SharedAudioReply(dictionary))
        dictionary["version"] = 2
        XCTAssertThrowsError(try SharedAudioReply(dictionary))
        dictionary["version"] = 1
        dictionary["newest"] = 1
        dictionary["packets"] = [["sequence": 1, "time": 1, "bytes": Data(repeating: 0, count: 513)]]
        XCTAssertThrowsError(try SharedAudioReply(dictionary))
    }

    func testOwnerFailureKeepsNativeErrorInsteadOfPretendingDeviceIsAbsent() throws {
        let endpoint = SharedEndpointFixture()
        let connection = try SharedAudioConnection(endpoint: endpoint)
        endpoint.failOwner()
        guard case .failed(let detail) = connection.readPacket() else { return XCTFail("Failure hidden") }
        XCTAssertTrue(detail.contains("0xE00002C5"))
        XCTAssertFalse(detail.contains("IOKIT_NO_DEVICE"))
        XCTAssertThrowsError(try SharedAudioConnection(endpoint: endpoint)) { error in
            guard case RigKontrolUSBReadError.operationFailed(let detail) = error else {
                return XCTFail("Expected actual owner failure, got \(error)")
            }
            XCTAssertTrue(detail.contains("0xE00002C5"))
        }
    }
}

private final class SharedWriteResult: @unchecked Sendable {
    private let lock = NSLock()
    private var storage: RigKontrolUSBWriteResult?
    var value: RigKontrolUSBWriteResult? { lock.lock(); defer { lock.unlock() }; return storage }
    func set(_ value: RigKontrolUSBWriteResult) { lock.lock(); storage = value; lock.unlock() }
}

private final class SharedEndpointFixture: SharedAudioEndpoint, @unchecked Sendable {
    private let condition = NSCondition()
    private let boot = UUID().uuidString
    private var epoch: UInt64 = 1
    private var sequence: UInt64 = 0
    private var packets: [[String: Any]] = []
    private var completions: [[String: Any]] = []
    private var request: [String: Any]?
    private var ownerFailed = false

    func read(_ qualifier: [String: Any]) throws -> [String: Any] {
        condition.lock(); defer { condition.unlock() }
        let after = (qualifier["after"] as? NSNumber)?.uint64Value ?? 0
        return [
            "version": 1, "boot": boot, "epoch": epoch,
            "state": ownerFailed ? "failed" : "ready",
            "error": ownerFailed ? Int32(bitPattern: 0xe00002c5) : 0,
            "oldest": packets.isEmpty ? sequence : 1, "newest": sequence,
            // Include history even on baseline, to verify the reader discards it.
            "packets": packets.filter { ($0["sequence"] as! UInt64) > after },
            "completions": completions,
        ]
    }

    func enqueue(_ request: [String: Any]) throws {
        condition.lock(); self.request = request; condition.broadcast(); condition.unlock()
    }

    func publish(_ bytes: [UInt8], skippingSequence: Bool = false) {
        condition.lock()
        sequence += skippingSequence ? 2 : 1
        packets.append(["sequence": sequence, "time": mach_absolute_time(), "bytes": Data(bytes)])
        condition.unlock()
    }

    func complete(actual: Int, wrongClient: Bool = false) {
        condition.lock(); defer { condition.unlock() }
        guard let request else { return }
        completions.append([
            "client": wrongClient ? UUID().uuidString : request["client"]!, "id": request["id"]!,
            "epoch": epoch, "status": 0, "actual": actual,
        ])
    }

    func reconnect() {
        condition.lock(); epoch += 1; sequence = 0; packets.removeAll(); completions.removeAll(); condition.unlock()
    }

    func failOwner() { condition.lock(); ownerFailed = true; condition.unlock() }

    func waitForRequest(timeout: TimeInterval = 1) -> Bool {
        condition.lock(); defer { condition.unlock() }
        let deadline = Date().addingTimeInterval(timeout)
        while request == nil { if !condition.wait(until: deadline) { return request != nil } }
        return true
    }
}
