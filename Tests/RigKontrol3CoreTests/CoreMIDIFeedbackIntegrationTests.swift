import CoreMIDI
import Foundation
import RigKontrol3Core
import XCTest

final class CoreMIDIFeedbackIntegrationTests: XCTestCase {
    /// Opt in only where the native CoreMIDI service is available; uses no hardware.
    func testLegacyMIDISendReachesFeedbackDestinationAndPreservesDiagnosticMessages() throws {
        guard ProcessInfo.processInfo.environment["RK3_RUN_MIDI_INTEGRATION"] == "1" else {
            throw XCTSkip("Set RK3_RUN_MIDI_INTEGRATION=1 outside the sandbox to test CoreMIDI feedback.")
        }

        let receipt = MIDIFeedbackReceipt()
        let name = "RK3 feedback integration \(UUID().uuidString)"
        var destination: CoreMIDIVirtualFeedbackDestination? = try CoreMIDIVirtualFeedbackDestination(
            name: name, uniqueID: nil, received: { receipt.receive($0) })
        let endpoint = try XCTUnwrap(findDestination(named: name))

        var client = MIDIClientRef()
        try check(MIDIClientCreate("RK3 feedback sender" as CFString, nil, nil, &client), "MIDIClientCreate")
        defer { MIDIClientDispose(client) }
        var output = MIDIPortRef()
        try check(MIDIOutputPortCreate(client, "Feedback sender" as CFString, &output), "MIDIOutputPortCreate")
        defer { MIDIPortDispose(output) }

        let messages: [[UInt8]] = [
            [0xBF, 20, 127], [0xBF, 20, 0], [0xBF, 21, 64], [0xB0, 20, 127],
        ]
        let storageSize = 256
        let storage = UnsafeMutableRawPointer.allocate(
            byteCount: storageSize, alignment: MemoryLayout<MIDIPacketList>.alignment)
        defer { storage.deallocate() }
        let list = storage.bindMemory(to: MIDIPacketList.self, capacity: 1)
        let initialPacket = MIDIPacketListInit(list)
        let bytes = messages.flatMap { $0 }
        let added = bytes.withUnsafeBufferPointer { buffer in
            MIDIPacketListAdd(list, storageSize, initialPacket, 0, bytes.count, buffer.baseAddress!)
        }
        XCTAssertNotNil(added)
        try withExtendedLifetime(destination) {
            try check(MIDISend(output, endpoint, UnsafePointer(list)), "MIDISend")
        }

        let received = try XCTUnwrap(receipt.wait(forCount: 4, timeout: 2), "No complete host feedback arrived")
        XCTAssertEqual(received.map(\.bytes), messages)
        let accepted = received.compactMap { RigKontrolLEDFeedback(message: $0) }
        XCTAssertEqual(accepted.map(\.switchIndex), [1, 1])
        XCTAssertEqual(accepted.map(\.isOn), [true, false])

        // The CoreMIDI-owned receive block retains its callback context, not the
        // destination itself. Disposing the owner must unpublish the endpoint.
        destination = nil
        XCTAssertNil(findDestination(named: name))
    }

    private func findDestination(named name: String) -> MIDIEndpointRef? {
        for index in 0..<MIDIGetNumberOfDestinations() {
            let endpoint = MIDIGetDestination(index)
            var property: Unmanaged<CFString>?
            if MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &property) == noErr,
                let property, property.takeRetainedValue() as String == name
            {
                return endpoint
            }
        }
        return nil
    }

    private func check(_ status: OSStatus, _ operation: String) throws {
        if status != noErr { throw CoreMIDIError.osStatus(status, operation: operation) }
    }
}

private final class MIDIFeedbackReceipt: @unchecked Sendable {
    private let condition = NSCondition()
    private var messages: [MIDIMessage] = []

    func receive(_ message: MIDIMessage) {
        condition.lock()
        messages.append(message)
        condition.broadcast()
        condition.unlock()
    }

    func wait(forCount count: Int, timeout: TimeInterval) -> [MIDIMessage]? {
        let deadline = Date().addingTimeInterval(timeout)
        condition.lock()
        defer { condition.unlock() }
        while messages.count < count {
            if !condition.wait(until: deadline) { return nil }
        }
        return messages
    }
}
