import Foundation

public struct MIDIMessage: Equatable, Sendable {
    public let bytes: [UInt8]

    public init(bytes: [UInt8]) {
        self.bytes = bytes
    }

    public static func noteOn(channel: MIDIChannel, note: Int, velocity: Int = 127) -> MIDIMessage {
        MIDIMessage(bytes: [0x90 | channel.zeroBasedValue, sevenBit(note), sevenBit(velocity)])
    }

    public static func noteOff(channel: MIDIChannel, note: Int, velocity: Int = 64) -> MIDIMessage {
        MIDIMessage(bytes: [0x80 | channel.zeroBasedValue, sevenBit(note), sevenBit(velocity)])
    }

    public static func controlChange(channel: MIDIChannel, controller: Int, value: Int)
        -> MIDIMessage
    {
        MIDIMessage(bytes: [0xB0 | channel.zeroBasedValue, sevenBit(controller), sevenBit(value)])
    }

    public var umpWord: UInt32 {
        precondition(
            bytes.count == 3, "Only three-byte MIDI 1.0 channel voice messages are supported")
        return (0x2 << 28) | (UInt32(bytes[0]) << 16) | (UInt32(bytes[1]) << 8) | UInt32(bytes[2])
    }

    private static func sevenBit(_ value: Int) -> UInt8 {
        UInt8(max(0, min(127, value)))
    }
}
