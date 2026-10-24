import Foundation
@testable import RigKontrol3Core
import XCTest

/// Opt in only after the reviewed combined HAL is installed and the controller
/// app is quit. Uses CoreAudio properties; never opens USB or creates a UI.
final class SharedAudioHALIntegrationTests: XCTestCase {
    func testInstalledHALDeliversActualQueryCompletionAndFreshSnapshot() throws {
        guard ProcessInfo.processInfo.environment["RK3_RUN_SHARED_HAL_INTEGRATION"] == "1" else {
            throw XCTSkip("Set RK3_RUN_SHARED_HAL_INTEGRATION=1 with the combined HAL installed and controller quit")
        }
        let started = ProcessInfo.processInfo.systemUptime
        var write: RigKontrolUSBWriteResult?
        var snapshot: RigKontrolUSBReadPacket?
        let summary = try SharedAudioEventReader.streamRigKontrol3BulkInput(
            didOpen: { write = $0.write([0x04]) },
            shouldContinue: {
                snapshot == nil && write?.status == "IOKIT_SUCCESS"
                    && ProcessInfo.processInfo.systemUptime - started < 3
            },
            packetHandler: { packet, _ in
                if packet.bytes.count == 8, packet.bytes[0] == 0x04,
                   packet.bytes[2...7].allSatisfy({ $0 == 0 }) { snapshot = packet }
            })
        XCTAssertEqual(write, .init(status: "IOKIT_SUCCESS", transferred: 1))
        XCTAssertTrue(summary.nonTimeoutErrors.isEmpty, summary.nonTimeoutErrors.joined(separator: "; "))
        XCTAssertNotNil(snapshot, "No fresh READ_IO response delivered through the installed CoreAudio property channel")
        if let snapshot { XCTAssertLessThan(abs(snapshot.timestamp.timeIntervalSinceNow), 1) }
    }
}
