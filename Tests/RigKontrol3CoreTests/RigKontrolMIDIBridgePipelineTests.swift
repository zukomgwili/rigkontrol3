import RigKontrol3Core
import XCTest

final class RigKontrolMIDIBridgePipelineTests: XCTestCase {
    func testMapsVerifiedSwitchPacketToMIDINote() {
        var pipeline = RigKontrolMIDIBridgePipeline()

        XCTAssertEqual(
            pipeline.outputs(for: [0x04, 0x10, 0x00, 0x00, 0x00, 0x00, 0x53, 0x04]),
            [
                RigKontrolMIDIOutput(
                    event: .switchChanged(index: 1, isPressed: true),
                    message: .noteOn(channel: MIDIChannel(16), note: 1)
                )
            ]
        )
    }

    func testMapsExpressionPacketToMIDIControlChange() {
        var pipeline = RigKontrolMIDIBridgePipeline(
            parser: RigKontrolPacketParser(expressionFilter: ExpressionValueFilter(deadband: 0))
        )

        XCTAssertEqual(
            pipeline.outputs(
                for: [
                    0x03, 0x00, 0x00, 0x00, 0x00, 0x02, 0x94, 0x04, 0x00, 0x00, 0xa2, 0xaf,
                    0x7b, 0xff, 0xcf, 0x9c, 0xdf, 0xfd, 0xed, 0xff, 0x1d, 0x9f, 0x59, 0x7f,
                    0xff, 0x13, 0x5c, 0x5d, 0xde, 0xbe, 0x09, 0xff, 0x56,
                ]
            ),
            [
                RigKontrolMIDIOutput(
                    event: .expressionChanged(normalizedValue: 127),
                    message: .controlChange(channel: MIDIChannel(16), controller: 11, value: 127)
                )
            ]
        )
    }
}
