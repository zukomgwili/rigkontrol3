import Foundation

public enum RigKontrolControlEvent: Equatable, Sendable {
    case switchChanged(index: Int, isPressed: Bool)
    case expressionChanged(normalizedValue: Int)
}

public struct RigKontrolMapping: Equatable, Sendable {
    public var channel: MIDIChannel
    public var switchNotes: [Int: Int]
    public var expressionController: Int

    public init(channel: MIDIChannel, switchNotes: [Int: Int], expressionController: Int) {
        self.channel = channel
        self.switchNotes = switchNotes
        self.expressionController = expressionController
    }

    public static let defaultControllerMapping = RigKontrolMapping(
        channel: .rigKontrolDefault,
        switchNotes: Dictionary(uniqueKeysWithValues: (1...9).map { ($0, $0) }),
        expressionController: 11
    )

    public func messages(for event: RigKontrolControlEvent) -> [MIDIMessage] {
        switch event {
        case .switchChanged(let index, let isPressed):
            guard let note = switchNotes[index] else {
                return []
            }
            return [
                isPressed
                    ? .noteOn(channel: channel, note: note) : .noteOff(channel: channel, note: note)
            ]
        case .expressionChanged(let normalizedValue):
            return [
                .controlChange(
                    channel: channel, controller: expressionController, value: normalizedValue)
            ]
        }
    }
}
