import CoreMIDI
import Darwin
import Foundation
import RigKontrol3Core
import XCTest

final class CoreMIDILegacyDeliveryIntegrationTests: XCTestCase {
    /// Requires the native CoreMIDI service; enable explicitly outside restricted sandboxes.
    func testVirtualSourceDeliversLegacyBytesWithCurrentHostTimestamp() throws {
        guard ProcessInfo.processInfo.environment["RK3_RUN_MIDI_INTEGRATION"] == "1" else {
            throw XCTSkip("Set RK3_RUN_MIDI_INTEGRATION=1 outside the sandbox to test real CoreMIDI delivery.")
        }

        let receiver = LegacyMIDIReceipt()
        let context = Unmanaged.passRetained(receiver)
        defer { context.release() }
        var client = MIDIClientRef()
        try check(MIDIClientCreate("RK3 legacy delivery test" as CFString, nil, nil, &client), "MIDIClientCreate")
        defer { MIDIClientDispose(client) }
        var input = MIDIPortRef()
        try check(MIDIInputPortCreate(
            client, "Legacy input" as CFString, legacyMIDIRead,
            context.toOpaque(), &input), "MIDIInputPortCreate")
        defer { MIDIPortDispose(input) }

        let source = try CoreMIDIVirtualSource(name: "RK3 integration \(UUID().uuidString)")
        let endpoint = try XCTUnwrap(findSource(named: source.name), "New virtual MIDI source was not published")
        try check(MIDIPortConnectSource(input, endpoint, nil), "MIDIPortConnectSource")
        defer { MIDIPortDisconnectSource(input, endpoint) }

        try withExtendedLifetime(source) {
            let beforeSend = mach_absolute_time()
            try source.send(.controlChange(channel: MIDIChannel(16), controller: 11, value: 37))
            let receipt = try XCTUnwrap(receiver.wait(timeout: 2), "Legacy input received no MIDI bytes")
            let afterReceipt = mach_absolute_time()
            XCTAssertEqual(receipt.bytes, [0xbf, 11, 37])
            XCTAssertGreaterThan(receipt.timestamp, 0, "Timestamp zero is not 'now' for MIDIReceivedEventList")
            XCTAssertGreaterThanOrEqual(receipt.timestamp, beforeSend, "MIDI event was stamped before this send")
            XCTAssertLessThanOrEqual(receipt.timestamp, afterReceipt, "MIDI event was stamped in the future")
        }
    }

    private func check(_ status: OSStatus, _ operation: String) throws {
        if status != noErr { throw CoreMIDIError.osStatus(status, operation: operation) }
    }

    private func findSource(named name: String) -> MIDIEndpointRef? {
        for index in 0..<MIDIGetNumberOfSources() {
            let endpoint = MIDIGetSource(index)
            var property: Unmanaged<CFString>?
            if MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &property) == noErr,
                let property, property.takeRetainedValue() as String == name
            {
                return endpoint
            }
        }
        return nil
    }
}

private final class LegacyMIDIReceipt: @unchecked Sendable {
    struct Packet {
        let bytes: [UInt8]
        let timestamp: MIDITimeStamp
    }
    private let condition = NSCondition()
    private var packet: Packet?

    func receive(_ packet: Packet) {
        condition.lock()
        if self.packet == nil { self.packet = packet }
        condition.broadcast()
        condition.unlock()
    }

    func wait(timeout: TimeInterval) -> Packet? {
        let deadline = Date().addingTimeInterval(timeout)
        condition.lock()
        defer { condition.unlock() }
        while packet == nil {
            if !condition.wait(until: deadline) { return packet }
        }
        return packet
    }
}

private func legacyMIDIRead(
    _ list: UnsafePointer<MIDIPacketList>, _ context: UnsafeMutableRawPointer?,
    _ connection: UnsafeMutableRawPointer?
) {
    guard let context,
        let packetOffset = MemoryLayout<MIDIPacketList>.offset(of: \.packet),
        let dataOffset = MemoryLayout<MIDIPacket>.offset(of: \.data)
    else { return }
    let receiver = Unmanaged<LegacyMIDIReceipt>.fromOpaque(context).takeUnretainedValue()
    var packet = UnsafeRawPointer(list).advanced(by: packetOffset).assumingMemoryBound(to: MIDIPacket.self)
    for _ in 0..<list.pointee.numPackets {
        let bytes = UnsafeRawPointer(packet).advanced(by: dataOffset).assumingMemoryBound(to: UInt8.self)
        receiver.receive(LegacyMIDIReceipt.Packet(
            bytes: Array(UnsafeBufferPointer(start: bytes, count: Int(packet.pointee.length))),
            timestamp: packet.pointee.timeStamp))
        packet = UnsafePointer(MIDIPacketNext(packet))
    }
}
