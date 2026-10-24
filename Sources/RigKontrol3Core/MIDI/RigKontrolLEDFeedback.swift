import Foundation

/// Host-confirmed states for the eight mapped switch LEDs.
public struct RigKontrolLEDFeedback: Equatable, Sendable {
    public let switchIndex: Int
    public let isOn: Bool

    public init?(message: MIDIMessage) {
        let bytes = message.bytes
        guard bytes.count == 3, bytes[0] == 0xBF,
            (20...27).contains(bytes[1]), bytes[2] == 0 || bytes[2] == 127
        else { return nil }

        switchIndex = Int(bytes[1]) - 19
        isOn = bytes[2] == 127
    }
}

/// Decodes MIDI 1.0 channel voice UMPs without interpreting another message's payload.
public enum MIDI1ChannelVoiceUMPDecoder {
    public static func messages(in words: [UInt32]) -> [MIDIMessage] {
        var messages: [MIDIMessage] = []
        var index = 0
        while index < words.count {
            let word = words[index]
            let type = word >> 28
            let wordCount: Int
            switch type {
            case 0x0, 0x1, 0x2: wordCount = 1
            case 0x3, 0x4: wordCount = 2
            case 0x5, 0xD, 0xF: wordCount = 4
            default:
                // Reserved types have no supported framing here. Do not scan
                // their payload looking for what could resemble a switch CC.
                return messages
            }
            guard words.count - index >= wordCount else { return messages }

            if type == 0x2, let message = channelVoiceMessage(word) {
                messages.append(message)
            }
            index += wordCount
        }
        return messages
    }

    private static func channelVoiceMessage(_ word: UInt32) -> MIDIMessage? {
        let status = UInt8((word >> 16) & 0xFF)
        let data1 = UInt8((word >> 8) & 0xFF)
        let data2 = UInt8(word & 0xFF)
        guard (0x80...0xEF).contains(status), data1 < 0x80, data2 < 0x80 else { return nil }
        switch status & 0xF0 {
        case 0xC0, 0xD0:
            // Program change and channel pressure have one data byte; their
            // unused UMP byte is reserved and must not become a third MIDI byte.
            guard data2 == 0 else { return nil }
            return MIDIMessage(bytes: [status, data1])
        default:
            return MIDIMessage(bytes: [status, data1, data2])
        }
    }
}
