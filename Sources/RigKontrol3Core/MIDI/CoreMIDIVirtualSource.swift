import CoreMIDI
import Darwin
import Foundation

public enum CoreMIDIError: Error, Equatable {
    case osStatus(OSStatus, operation: String)
}

public final class CoreMIDIVirtualSource {
    public let name: String

    private var client = MIDIClientRef()
    private var source = MIDIEndpointRef()

    public init(
        name: String = "RigKontrol3 Bridge", protocolID: MIDIProtocolID = ._1_0,
        uniqueID: MIDIUniqueID? = nil
    ) throws {
        self.name = name

        var createdClient = MIDIClientRef()
        let clientStatus = MIDIClientCreate(name as CFString, nil, nil, &createdClient)
        guard clientStatus == noErr else {
            throw CoreMIDIError.osStatus(clientStatus, operation: "MIDIClientCreate")
        }

        var createdSource = MIDIEndpointRef()
        let sourceStatus = MIDISourceCreateWithProtocol(
            createdClient, name as CFString, protocolID, &createdSource)
        guard sourceStatus == noErr else {
            MIDIClientDispose(createdClient)
            throw CoreMIDIError.osStatus(sourceStatus, operation: "MIDISourceCreateWithProtocol")
        }

        if let uniqueID {
            let identityStatus = MIDIObjectSetIntegerProperty(
                createdSource, kMIDIPropertyUniqueID, uniqueID)
            guard identityStatus == noErr else {
                MIDIEndpointDispose(createdSource)
                MIDIClientDispose(createdClient)
                throw CoreMIDIError.osStatus(identityStatus, operation: "MIDIObjectSetIntegerProperty")
            }
        }

        self.client = createdClient
        self.source = createdSource
    }

    deinit {
        if source != 0 {
            MIDIEndpointDispose(source)
        }
        if client != 0 {
            MIDIClientDispose(client)
        }
    }

    public func send(_ message: MIDIMessage) throws {
        let byteCount = 256
        let storage = UnsafeMutableRawPointer.allocate(
            byteCount: byteCount,
            alignment: MemoryLayout<MIDIEventList>.alignment
        )
        defer { storage.deallocate() }

        let eventList = storage.bindMemory(to: MIDIEventList.self, capacity: 1)
        let packet = MIDIEventListInit(eventList, ._1_0)
        var word = message.umpWord
        _ = withUnsafePointer(to: &word) { wordPointer in
            // MIDIReceivedEventList requires an actual host timestamp; unlike
            // MIDISendEventList, zero does not mean "now" for a virtual source.
            MIDIEventListAdd(eventList, byteCount, packet, mach_absolute_time(), 1, wordPointer)
        }

        let status = MIDIReceivedEventList(source, UnsafePointer(eventList))
        guard status == noErr else {
            throw CoreMIDIError.osStatus(status, operation: "MIDIReceivedEventList")
        }
    }
}
