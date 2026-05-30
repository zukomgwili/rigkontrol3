import RigKontrol3Core
import XCTest

final class LibUSBBulkPacketAnalysisTests: XCTestCase {
    func testObservationsAreEmptyWhenThereAreNoPackets() {
        XCTAssertEqual(LibUSBBulkPacketAnalyzer.observations(for: []), [])
    }

    func testFirstPacketIsABaselineObservation() {
        let packet = packet(at: Date(timeIntervalSince1970: 100), bytes: [0x03, 0x00, 0x54])

        let observations = LibUSBBulkPacketAnalyzer.observations(for: [packet])

        XCTAssertEqual(observations.count, 1)
        XCTAssertEqual(observations[0].packetIndex, 0)
        XCTAssertEqual(observations[0].relativeTime, 0)
        XCTAssertEqual(observations[0].length, 3)
        XCTAssertEqual(observations[0].changedBytes, [])
        XCTAssertEqual(observations[0].bytes, [0x03, 0x00, 0x54])
    }

    func testObservationReportsRelativeTimeAndChangedOffsets() {
        let start = Date(timeIntervalSince1970: 100)
        let packets = [
            packet(at: start, bytes: [0x03, 0x00, 0x54, 0x04]),
            packet(at: start.addingTimeInterval(0.25), bytes: [0x03, 0x01, 0x53, 0x04]),
        ]

        let observations = LibUSBBulkPacketAnalyzer.observations(for: packets)

        XCTAssertEqual(observations[1].packetIndex, 1)
        XCTAssertEqual(observations[1].relativeTime, 0.25, accuracy: 0.001)
        XCTAssertEqual(
            observations[1].changedBytes,
            [
                LibUSBBulkPacketByteChange(
                    offset: 1,
                    previousValue: 0x00,
                    currentValue: 0x01
                ),
                LibUSBBulkPacketByteChange(
                    offset: 2,
                    previousValue: 0x54,
                    currentValue: 0x53
                ),
            ]
        )
    }

    func testFirstPacketCanCompareAgainstPreviousBytes() {
        let packet = packet(at: Date(timeIntervalSince1970: 100), bytes: [0x03, 0x01, 0x54])

        let observations = LibUSBBulkPacketAnalyzer.observations(
            for: [packet],
            previousBytes: [0x03, 0x00, 0x54]
        )

        XCTAssertEqual(
            observations[0].changedBytes,
            [
                LibUSBBulkPacketByteChange(
                    offset: 1,
                    previousValue: 0x00,
                    currentValue: 0x01
                )
            ]
        )
    }

    func testByteChangesIncludeLengthDifferences() {
        XCTAssertEqual(
            LibUSBBulkPacketAnalyzer.byteChanges(
                previous: [0x01, 0x02, 0x03],
                current: [0x01, 0x09]
            ),
            [
                LibUSBBulkPacketByteChange(
                    offset: 1,
                    previousValue: 0x02,
                    currentValue: 0x09
                ),
                LibUSBBulkPacketByteChange(
                    offset: 2,
                    previousValue: 0x03,
                    currentValue: nil
                ),
            ]
        )
    }

    private func packet(at timestamp: Date, bytes: [UInt8]) -> LibUSBBulkReadPacket {
        LibUSBBulkReadPacket(endpointAddress: 0x81, timestamp: timestamp, bytes: bytes)
    }
}
