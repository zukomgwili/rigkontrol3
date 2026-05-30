import Foundation

public struct RigKontrolMIDIOutput: Equatable, Sendable {
    public let event: RigKontrolControlEvent
    public let message: MIDIMessage

    public init(event: RigKontrolControlEvent, message: MIDIMessage) {
        self.event = event
        self.message = message
    }
}

public struct RigKontrolMIDIBridgePipeline: Equatable, Sendable {
    public var parser: RigKontrolPacketParser
    public var mapping: RigKontrolMapping

    public init(
        parser: RigKontrolPacketParser = RigKontrolPacketParser(),
        mapping: RigKontrolMapping = .defaultControllerMapping
    ) {
        self.parser = parser
        self.mapping = mapping
    }

    public mutating func outputs(for packet: [UInt8]) -> [RigKontrolMIDIOutput] {
        parser.events(for: packet).flatMap { event in
            mapping.messages(for: event).map { message in
                RigKontrolMIDIOutput(event: event, message: message)
            }
        }
    }
}
