import Foundation

public struct RigKontrolControllerFrame: Equatable, Sendable {
    public let messages: [MIDIMessage]
    public let ledState: RigKontrolLEDState
    public let ledsChanged: Bool

    public init(messages: [MIDIMessage], ledState: RigKontrolLEDState, ledsChanged: Bool) {
        self.messages = messages
        self.ledState = ledState
        self.ledsChanged = ledsChanged
    }
}

/// Processes one USB attachment without assuming any state in the MIDI host.
public struct RigKontrolControllerProcessor: Sendable {
    private var parser: RigKontrolPacketParser
    private var initiallyHeldSwitches: Set<Int> = []
    private var ledState = RigKontrolLEDState()

    public init(expressionCalibration: ExpressionCalibration = ExpressionCalibration()) {
        parser = RigKontrolPacketParser(expressionCalibration: expressionCalibration)
    }

    public mutating func setExpressionCalibration(_ calibration: ExpressionCalibration) {
        guard parser.expressionCalibration != calibration else { return }
        parser.expressionCalibration = calibration
        parser.expressionFilter = ExpressionValueFilter(deadband: parser.expressionFilter.deadband)
    }

    public mutating func process(_ bytes: [UInt8]) -> RigKontrolControllerFrame {
        let previousLEDState = ledState
        let isInitialSwitchReport = parser.previousSwitchMask == nil
            && RigKontrolPacketParser.switchMask(from: bytes) != nil
        let events = parser.events(for: bytes)

        if isInitialSwitchReport {
            // A switch already held on attachment must complete its release before
            // a new gesture can affect the host or light a physical press LED.
            initiallyHeldSwitches = Set(events.compactMap { event in
                guard case .switchChanged(let index, true) = event else { return nil }
                return index
            })
            return RigKontrolControllerFrame(
                messages: [], ledState: ledState, ledsChanged: false)
        }

        var messages: [MIDIMessage] = []
        for event in events {
            switch event {
            case .switchChanged(let index, let isPressed):
                if initiallyHeldSwitches.contains(index) {
                    if !isPressed {
                        initiallyHeldSwitches.remove(index)
                    }
                    continue
                }
                messages.append(.controlChange(
                    channel: .rigKontrolDefault, controller: 19 + index,
                    value: isPressed ? 127 : 0))
                ledState.setSwitchLED(index: index, isOn: isPressed)
            case .expressionChanged(let normalizedValue):
                messages.append(.controlChange(
                    channel: .rigKontrolDefault, controller: 11, value: normalizedValue))
            }
        }

        return RigKontrolControllerFrame(
            messages: messages, ledState: ledState, ledsChanged: ledState != previousLEDState)
    }
}
