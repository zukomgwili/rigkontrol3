import RigKontrol3Core
import XCTest

final class RigKontrolLEDFeedbackTests: XCTestCase {
    func testEveryMappedSwitchAcceptsOnlyHostBinaryOnOffValues() throws {
        for index in 1...8 {
            for value in [0, 127] {
                let feedback = try XCTUnwrap(RigKontrolLEDFeedback(message: .controlChange(
                    channel: MIDIChannel(16), controller: 19 + index, value: value)))
                XCTAssertEqual(feedback.switchIndex, index)
                XCTAssertEqual(feedback.isOn, value == 127)
            }
        }
    }

    func testRejectsUnrelatedChannelsControllersAndNonbinaryValues() {
        for channel in 1...15 {
            XCTAssertNil(RigKontrolLEDFeedback(message: .controlChange(
                channel: MIDIChannel(channel), controller: 20, value: 127)))
        }
        for controller in [0, 11, 19, 28, 127] {
            XCTAssertNil(RigKontrolLEDFeedback(message: .controlChange(
                channel: MIDIChannel(16), controller: controller, value: 127)))
        }
        for value in 1...126 {
            XCTAssertNil(RigKontrolLEDFeedback(message: .controlChange(
                channel: MIDIChannel(16), controller: 20, value: value)))
        }
    }

    func testRejectsMalformedAndNonCCMessages() {
        for bytes: [UInt8] in [
            [], [0xBF], [0xBF, 20], [0xBF, 20, 127, 0],
            [0x9F, 20, 127], [0xBF, 0x94, 127], [0xBF, 20, 0xFF],
        ] {
            XCTAssertNil(RigKontrolLEDFeedback(message: MIDIMessage(bytes: bytes)))
        }
    }

    func testDecodesAllMIDI1ChannelVoiceKindsAndKeepsUnmappedCCForDiagnostics() {
        let words: [UInt32] = [
            0x20803C40, 0x20903C7F, 0x20A03C30, 0x20BF147F,
            0x20C00500, 0x20D03000, 0x20E00040, 0x20B00B25,
        ]
        XCTAssertEqual(MIDI1ChannelVoiceUMPDecoder.messages(in: words).map(\.bytes), [
            [0x80, 60, 64], [0x90, 60, 127], [0xA0, 60, 48], [0xBF, 20, 127],
            [0xC0, 5], [0xD0, 48], [0xE0, 0, 64], [0xB0, 11, 37],
        ])
    }

    func testSkipsWholeKnownMultiwordMessagesRatherThanMistakingPayloadForFeedback() {
        let fakeCC: UInt32 = 0x20BF147F
        let words: [UInt32] = [
            0x00000000, 0x10F80000,
            0x30000000, fakeCC,
            0x40903C00, fakeCC,
            0x50000000, fakeCC, fakeCC, fakeCC,
            0xD0000000, fakeCC, fakeCC, fakeCC,
            0xF0000000, fakeCC, fakeCC, fakeCC,
            0x20BF1400,
        ]
        XCTAssertEqual(MIDI1ChannelVoiceUMPDecoder.messages(in: words).map(\.bytes), [[0xBF, 20, 0]])
    }

    func testReservedTypeStopsDecodingWithoutScanningItsPayload() {
        for type: UInt32 in [0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xC, 0xE] {
            XCTAssertEqual(MIDI1ChannelVoiceUMPDecoder.messages(in: [
                0x20BF1400, type << 28, 0x20BF147F,
            ]).map(\.bytes), [[0xBF, 20, 0]])
        }
    }

    func testTruncatedMultiwordMessagesDoNotDecodePayloadAsCC() {
        for prefix: [UInt32] in [
            [0x30000000], [0x40000000], [0x50000000, 0x20BF147F],
            [0xD0000000, 0x20BF147F, 0x20BF147F], [0xF0000000],
        ] {
            XCTAssertEqual(MIDI1ChannelVoiceUMPDecoder.messages(in: prefix), [])
        }
    }

    func testMalformedChannelVoiceWordsAreRejectedWithoutLosingNextMessage() {
        let words: [UInt32] = [
            0x207F147F, 0x20FF147F, 0x20BF947F, 0x20BF14FF,
            0x20C00501, 0x20D03001, 0x20BF147F,
        ]
        XCTAssertEqual(MIDI1ChannelVoiceUMPDecoder.messages(in: words).map(\.bytes), [[0xBF, 20, 127]])
    }
}
