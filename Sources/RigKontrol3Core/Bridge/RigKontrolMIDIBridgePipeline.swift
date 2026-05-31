import Foundation

public struct RigKontrolMIDIOutput: Equatable, Sendable {
    public let event: RigKontrolControlEvent
    public let message: MIDIMessage

    public init(event: RigKontrolControlEvent, message: MIDIMessage) {
        self.event = event
        self.message = message
    }
}

public enum RigKontrolSwitchMIDIMode: String, CaseIterable, Sendable {
    case latch
    case momentary
    case trigger
}

public struct RigKontrolMIDIBridgePipeline: Equatable, Sendable {
    public var parser: RigKontrolPacketParser
    public var mapping: RigKontrolMapping
    public var switchMode: RigKontrolSwitchMIDIMode
    public private(set) var latchedSwitchStates: [Int: Bool]

    public init(
        parser: RigKontrolPacketParser = RigKontrolPacketParser(),
        mapping: RigKontrolMapping = .defaultControllerMapping,
        switchMode: RigKontrolSwitchMIDIMode = .momentary,
        latchedSwitchStates: [Int: Bool] = [:]
    ) {
        self.parser = parser
        self.mapping = mapping
        self.switchMode = switchMode
        self.latchedSwitchStates = latchedSwitchStates
    }

    public mutating func outputs(for packet: [UInt8]) -> [RigKontrolMIDIOutput] {
        parser.events(for: packet).flatMap { event in
            outputs(for: event)
        }
    }

    private mutating func outputs(for event: RigKontrolControlEvent) -> [RigKontrolMIDIOutput] {
        switch event {
        case .expressionChanged:
            return midiOutputs(for: event)
        case .switchChanged(let index, let isPressed):
            switch switchMode {
            case .momentary:
                return midiOutputs(for: event)
            case .trigger:
                guard isPressed else {
                    return []
                }
                return midiOutputs(for: event)
            case .latch:
                guard isPressed else {
                    return []
                }
                let latchedState = !(latchedSwitchStates[index] ?? false)
                latchedSwitchStates[index] = latchedState
                return midiOutputs(
                    for: .switchChanged(index: index, isPressed: latchedState)
                )
            }
        }
    }

    private func midiOutputs(for event: RigKontrolControlEvent) -> [RigKontrolMIDIOutput] {
        mapping.messages(for: event).map { message in
            RigKontrolMIDIOutput(event: event, message: message)
        }
    }
}
