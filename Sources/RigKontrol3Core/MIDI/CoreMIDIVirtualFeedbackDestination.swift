import CoreMIDI
import Foundation

/// A separate host-to-controller port. Its name matches the source so MainStage
/// can pair the endpoints for feedback; its persistent unique ID stays distinct.
/// Receiving feedback never sends MIDI or USB commands.
public final class CoreMIDIVirtualFeedbackDestination {
    public let name: String

    private var client = MIDIClientRef()
    private var destination = MIDIEndpointRef()
    private let receiver: MIDIFeedbackReceiver

    public init(
        name: String = "RigKontrol3 Bridge", uniqueID: MIDIUniqueID? = 0x524B3302,
        received: @escaping @Sendable (MIDIMessage) -> Void
    ) throws {
        self.name = name
        let receiver = MIDIFeedbackReceiver(received: received)
        self.receiver = receiver

        var createdClient = MIDIClientRef()
        let clientStatus = MIDIClientCreate(name as CFString, nil, nil, &createdClient)
        guard clientStatus == noErr else {
            throw CoreMIDIError.osStatus(clientStatus, operation: "MIDIClientCreate feedback")
        }

        var createdDestination = MIDIEndpointRef()
        let destinationStatus = MIDIDestinationCreateWithProtocol(
            createdClient, name as CFString, ._1_0, &createdDestination
        ) { [receiver] eventList, _ in
            receiver.receive(eventList)
        }
        guard destinationStatus == noErr else {
            receiver.deactivate()
            MIDIClientDispose(createdClient)
            throw CoreMIDIError.osStatus(
                destinationStatus, operation: "MIDIDestinationCreateWithProtocol")
        }

        if let uniqueID {
            let identityStatus = MIDIObjectSetIntegerProperty(
                createdDestination, kMIDIPropertyUniqueID, uniqueID)
            guard identityStatus == noErr else {
                receiver.deactivate()
                MIDIEndpointDispose(createdDestination)
                MIDIClientDispose(createdClient)
                throw CoreMIDIError.osStatus(
                    identityStatus, operation: "MIDIObjectSetIntegerProperty feedback")
            }
        }

        client = createdClient
        destination = createdDestination
    }

    deinit {
        receiver.deactivate()
        if destination != 0 { MIDIEndpointDispose(destination) }
        if client != 0 { MIDIClientDispose(client) }
    }
}

/// CoreMIDI retains this block-owned context, including during an in-flight callback.
/// Teardown clears future deliveries; an already copied callback remains safely retained.
private final class MIDIFeedbackReceiver: @unchecked Sendable {
    private let lock = NSLock()
    private var received: (@Sendable (MIDIMessage) -> Void)?

    init(received: @escaping @Sendable (MIDIMessage) -> Void) {
        self.received = received
    }

    func deactivate() {
        lock.lock()
        received = nil
        lock.unlock()
    }

    func receive(_ eventList: UnsafePointer<MIDIEventList>) {
        guard let packetOffset = MemoryLayout<MIDIEventList>.offset(of: \.packet),
            let wordsOffset = MemoryLayout<MIDIEventPacket>.offset(of: \.words)
        else { return }
        var packet = UnsafeRawPointer(eventList).advanced(by: packetOffset)
            .assumingMemoryBound(to: MIDIEventPacket.self)
        for _ in 0..<eventList.pointee.numPackets {
            let words = UnsafeRawPointer(packet).advanced(by: wordsOffset)
                .assumingMemoryBound(to: UInt32.self)
            let messages = MIDI1ChannelVoiceUMPDecoder.messages(in: Array(
                UnsafeBufferPointer(start: words, count: Int(packet.pointee.wordCount))))
            for message in messages {
                lock.lock()
                let callback = received
                lock.unlock()
                callback?(message)
            }
            packet = UnsafePointer(MIDIEventPacketNext(packet))
        }
    }
}
