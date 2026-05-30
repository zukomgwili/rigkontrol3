import RigKontrol3Core
import XCTest

final class RigKontrolGuidedCaptureRecordTests: XCTestCase {
    func testRecordIncludesStepMetadataPacketBytesAndChanges() {
        let step = RigKontrolGuidedCaptureStep(
            label: "switch 1",
            instruction: "Press and hold footswitch 1 until the next step label appears."
        )
        let observation = LibUSBBulkPacketObservation(
            packetIndex: 0,
            endpointAddress: 0x81,
            timestamp: Date(timeIntervalSince1970: 100),
            relativeTime: 0.25,
            length: 3,
            changedBytes: [
                LibUSBBulkPacketByteChange(
                    offset: 1,
                    previousValue: 0x00,
                    currentValue: 0x7f
                )
            ],
            bytes: [0x03, 0x7f, 0x54]
        )

        let record = RigKontrolGuidedCaptureRecord(
            stepIndex: 1,
            stepCount: 12,
            step: step,
            observation: observation
        )

        XCTAssertEqual(record.schemaVersion, 1)
        XCTAssertEqual(record.stepIndex, 1)
        XCTAssertEqual(record.stepNumber, 2)
        XCTAssertEqual(record.stepCount, 12)
        XCTAssertEqual(record.stepLabel, "switch 1")
        XCTAssertEqual(record.packetIndex, 0)
        XCTAssertEqual(record.packetNumber, 1)
        XCTAssertEqual(record.timestampSecondsSince1970, 100)
        XCTAssertEqual(record.relativeTime, 0.25)
        XCTAssertEqual(record.endpointAddress, 0x81)
        XCTAssertEqual(record.length, 3)
        XCTAssertEqual(record.bytes, [0x03, 0x7f, 0x54])
        XCTAssertEqual(record.bytesHex, "03 7f 54")
        XCTAssertEqual(
            record.changes,
            [
                RigKontrolGuidedCaptureChangeRecord(
                    offset: 1,
                    previousValue: 0x00,
                    currentValue: 0x7f
                )
            ]
        )
    }

    func testRecordEncodesAsJSON() throws {
        let record = RigKontrolGuidedCaptureRecord(
            stepIndex: 0,
            stepCount: 12,
            step: RigKontrolGuidedCaptureStep(
                label: "baseline",
                instruction: "Leave all switches untouched and keep the expression pedal still."
            ),
            observation: LibUSBBulkPacketObservation(
                packetIndex: 0,
                endpointAddress: 0x81,
                timestamp: Date(timeIntervalSince1970: 100),
                relativeTime: 0,
                length: 2,
                changedBytes: [],
                bytes: [0x03, 0x54]
            )
        )

        let data = try JSONEncoder().encode(record)
        let json = String(decoding: data, as: UTF8.self)

        XCTAssertTrue(json.contains("\"schemaVersion\":1"))
        XCTAssertTrue(json.contains("\"stepLabel\":\"baseline\""))
        XCTAssertTrue(json.contains("\"bytesHex\":\"03 54\""))
    }
}
