import RigKontrol3Core
import XCTest

final class RigKontrolPacketParserTests: XCTestCase {
    func testParsesVerifiedSwitchPressAndReleaseFromSwitchReportPackets() {
        var parser = RigKontrolPacketParser()

        XCTAssertEqual(
            parser.events(for: [0x04, 0x10, 0x00, 0x00, 0x00, 0x00, 0x53, 0x04]),
            [.switchChanged(index: 1, isPressed: true)]
        )
        XCTAssertEqual(
            parser.events(for: [0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x04]),
            [.switchChanged(index: 1, isPressed: false)]
        )
    }

    func testParsesVerifiedSwitchTwoFromCleanCaptureEvidence() {
        var parser = RigKontrolPacketParser()

        XCTAssertEqual(
            parser.events(for: [0x04, 0x20, 0x00, 0x00, 0x00, 0x00, 0x53, 0x04]),
            [.switchChanged(index: 2, isPressed: true)]
        )
        XCTAssertEqual(
            parser.events(for: [0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x04]),
            [.switchChanged(index: 2, isPressed: false)]
        )
    }

    func testParsesVerifiedSwitchEightFromFocusedCaptureEvidence() {
        var parser = RigKontrolPacketParser()

        XCTAssertEqual(
            parser.events(for: [0x04, 0x08, 0x00, 0x00, 0x00, 0x02, 0x5f, 0x04]),
            [.switchChanged(index: 8, isPressed: true)]
        )
        XCTAssertEqual(
            parser.events(for: [0x04, 0x00, 0x00, 0x00, 0x00, 0x02, 0x60, 0x04]),
            [.switchChanged(index: 8, isPressed: false)]
        )
    }

    func testParsesMultipleVerifiedSwitchBitsInStableOrder() {
        var parser = RigKontrolPacketParser()

        XCTAssertEqual(
            parser.events(for: [0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x53, 0x04]),
            [
                .switchChanged(index: 5, isPressed: true),
                .switchChanged(index: 6, isPressed: true),
            ]
        )
    }

    func testSwitchNineHasNoDefaultSwitchMaskMapping() {
        var parser = RigKontrolPacketParser()

        XCTAssertEqual(
            parser.events(for: [0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x04]),
            []
        )
    }

    func testAllowsAlternateSwitchMappingsWhenConfigured() {
        var parser = RigKontrolPacketParser(
            switchBitMap: [0x20: 9]
        )

        XCTAssertEqual(
            parser.events(for: [0x04, 0x20, 0x00, 0x00, 0x00, 0x00, 0x53, 0x04]),
            [.switchChanged(index: 9, isPressed: true)]
        )
    }

    func testParsesExpressionRawValueFromExpressionReportPackets() {
        XCTAssertEqual(
            RigKontrolPacketParser.rawExpressionValue(
                from: [0x03, 0x00, 0x00, 0x00, 0x00, 0x02, 0x74, 0x04]
            ),
            628
        )
    }

    func testParsesExpressionEventsUsingCaptureDerivedCalibration() {
        var parser = RigKontrolPacketParser(expressionFilter: ExpressionValueFilter(deadband: 0))

        XCTAssertEqual(
            parser.events(
                for: [
                    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x04, 0x00, 0x00, 0xa2, 0xaf,
                    0x7b, 0xff, 0xcf, 0x9c, 0xdf, 0xfd, 0xed, 0xff, 0x1d, 0x9f, 0x59, 0x7f,
                    0xff, 0x13, 0x5c, 0x5d, 0xde, 0xbe, 0x09, 0xff, 0x56,
                ]
            ),
            [.expressionChanged(normalizedValue: 0)]
        )
        XCTAssertEqual(
            parser.events(
                for: [
                    0x03, 0x00, 0x00, 0x00, 0x00, 0x02, 0x94, 0x04, 0x00, 0x00, 0xa2, 0xaf,
                    0x7b, 0xff, 0xcf, 0x9c, 0xdf, 0xfd, 0xed, 0xff, 0x1d, 0x9f, 0x59, 0x7f,
                    0xff, 0x13, 0x5c, 0x5d, 0xde, 0xbe, 0x09, 0xff, 0x56,
                ]
            ),
            [.expressionChanged(normalizedValue: 127)]
        )
    }

    func testParsedSwitchEventFeedsDefaultMIDIMapping() {
        var parser = RigKontrolPacketParser()
        let mapping = RigKontrolMapping.defaultControllerMapping

        let events = parser.events(for: [0x04, 0x10, 0x00, 0x00, 0x00, 0x00, 0x53, 0x04])
        let messages = events.flatMap(mapping.messages(for:))

        XCTAssertEqual(messages, [.noteOn(channel: MIDIChannel(16), note: 1)])
    }

    func testParsedSwitchTwoFeedsDefaultMIDIMapping() {
        var parser = RigKontrolPacketParser()
        let mapping = RigKontrolMapping.defaultControllerMapping

        let events = parser.events(for: [0x04, 0x20, 0x00, 0x00, 0x00, 0x00, 0x53, 0x04])
        let messages = events.flatMap(mapping.messages(for:))

        XCTAssertEqual(messages, [.noteOn(channel: MIDIChannel(16), note: 2)])
    }
}
