import Foundation
@testable import RigKontrol3Core
import XCTest

final class LibUSBBulkInputPumpTests: XCTestCase {
    func testInputContinuesDrainingWhileOwnerPerformsConsecutiveWrites() {
        let device = BackpressuredDevice()
        XCTAssertFalse(device.write())
        let pump = LibUSBBulkInputPump(readPacket: { device.read() })
        defer { pump.stopAndWait() }
        XCTAssertNil(pump.start())

        // Each command produces input which must be drained before that write
        // can finish. A single priming read cannot complete these commands.
        for _ in 0..<3 { XCTAssertTrue(device.write()) }

        let delivered = (0..<4).compactMap { _ in pump.nextPacket()?.bytes.first }
        XCTAssertEqual(delivered, [0, 1, 2, 3])
        XCTAssertNil(pump.failure)
    }

    func testOverflowStopsWithExplicitErrorInsteadOfDroppingPacketsSilently() {
        let produced = DispatchSemaphore(value: 0)
        let pump = LibUSBBulkInputPump(capacity: 2) {
            produced.signal()
            return .packet(LibUSBBulkReadPacket(
                endpointAddress: 0x81, timestamp: Date(), bytes: [0x04, 0]))
        }
        _ = pump.start()
        // The producer fills the queue without an owner consuming it.
        for _ in 0..<3 { XCTAssertEqual(produced.wait(timeout: .now() + 1), .success) }
        pump.stopAndWait()
        XCTAssertTrue(pump.failure?.contains("queue overflow") == true)
        XCTAssertNil(pump.nextPacket())
    }
}

/// Minimal transport backpressure: OUT remains pending until IN is serviced.
private final class BackpressuredDevice: @unchecked Sendable {
    private let condition = NSCondition()
    private var inputPending = true
    private var canWrite = false
    private var sequence: UInt8 = 0

    func read() -> LibUSBBulkInputReadResult {
        condition.lock()
        defer { condition.unlock() }
        if !inputPending {
            _ = condition.wait(until: Date().addingTimeInterval(0.02))
        }
        guard inputPending else { return .idle }
        inputPending = false
        canWrite = true
        let packet = LibUSBBulkReadPacket(endpointAddress: 0x81, timestamp: Date(), bytes: [sequence])
        sequence &+= 1
        condition.broadcast()
        return .packet(packet)
    }

    func write() -> Bool {
        condition.lock()
        defer { condition.unlock() }
        guard canWrite else { return false }
        canWrite = false
        inputPending = true
        condition.broadcast()
        let deadline = Date().addingTimeInterval(0.2)
        while !canWrite {
            if !condition.wait(until: deadline) { return canWrite }
        }
        return true
    }
}
