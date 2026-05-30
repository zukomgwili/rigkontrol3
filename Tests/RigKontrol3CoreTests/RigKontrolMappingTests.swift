import RigKontrol3Core
import XCTest

final class RigKontrolMappingTests: XCTestCase {
    func testDefaultSwitchMappingEmitsNotesOnChannel16() {
        let mapping = RigKontrolMapping.defaultControllerMapping

        XCTAssertEqual(
            mapping.messages(for: .switchChanged(index: 1, isPressed: true)),
            [.noteOn(channel: MIDIChannel(16), note: 1)]
        )
        XCTAssertEqual(
            mapping.messages(for: .switchChanged(index: 1, isPressed: false)),
            [.noteOff(channel: MIDIChannel(16), note: 1)]
        )
    }

    func testUnknownSwitchProducesNoMessages() {
        let mapping = RigKontrolMapping.defaultControllerMapping

        XCTAssertEqual(mapping.messages(for: .switchChanged(index: 10, isPressed: true)), [])
    }

    func testDefaultExpressionMappingUsesCC11() {
        let mapping = RigKontrolMapping.defaultControllerMapping

        let messages = mapping.messages(for: .expressionChanged(normalizedValue: 64))

        XCTAssertEqual(
            messages, [.controlChange(channel: MIDIChannel(16), controller: 11, value: 64)])
    }

    func testMIDIMessageClampsDataBytesToSevenBitRange() {
        XCTAssertEqual(
            MIDIMessage.controlChange(channel: MIDIChannel(1), controller: 300, value: -10).bytes,
            [0xB0, 127, 0]
        )
    }

    func testMIDIMessageBuildsUMPWordForCoreMIDI() {
        XCTAssertEqual(MIDIMessage.noteOn(channel: MIDIChannel(1), note: 1).umpWord, 0x2090_017F)
    }
}
