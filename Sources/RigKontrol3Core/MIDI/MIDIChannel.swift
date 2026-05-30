import Foundation

public struct MIDIChannel: Equatable, Sendable {
    public let value: Int

    public init(_ value: Int) {
        precondition((1...16).contains(value), "MIDI channels are 1...16")
        self.value = value
    }

    public var zeroBasedValue: UInt8 {
        UInt8(value - 1)
    }

    public static let rigKontrolDefault = MIDIChannel(16)
}
