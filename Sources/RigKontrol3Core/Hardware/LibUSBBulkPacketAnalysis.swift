import Foundation

public struct LibUSBBulkPacketByteChange: Equatable, Sendable {
    public let offset: Int
    public let previousValue: UInt8?
    public let currentValue: UInt8?

    public init(offset: Int, previousValue: UInt8?, currentValue: UInt8?) {
        self.offset = offset
        self.previousValue = previousValue
        self.currentValue = currentValue
    }
}

public struct LibUSBBulkPacketObservation: Equatable, Sendable {
    public let packetIndex: Int
    public let endpointAddress: Int
    public let timestamp: Date
    public let relativeTime: TimeInterval
    public let length: Int
    public let changedBytes: [LibUSBBulkPacketByteChange]
    public let bytes: [UInt8]

    public init(
        packetIndex: Int,
        endpointAddress: Int,
        timestamp: Date,
        relativeTime: TimeInterval,
        length: Int,
        changedBytes: [LibUSBBulkPacketByteChange],
        bytes: [UInt8]
    ) {
        self.packetIndex = packetIndex
        self.endpointAddress = endpointAddress
        self.timestamp = timestamp
        self.relativeTime = relativeTime
        self.length = length
        self.changedBytes = changedBytes
        self.bytes = bytes
    }
}

public enum LibUSBBulkPacketAnalyzer {
    public static func observations(
        for packets: [LibUSBBulkReadPacket],
        previousBytes: [UInt8]? = nil
    )
        -> [LibUSBBulkPacketObservation]
    {
        guard let firstTimestamp = packets.first?.timestamp else {
            return []
        }

        return packets.enumerated().map { index, packet in
            let comparisonBytes = index == 0 ? previousBytes : packets[index - 1].bytes
            return LibUSBBulkPacketObservation(
                packetIndex: index,
                endpointAddress: packet.endpointAddress,
                timestamp: packet.timestamp,
                relativeTime: packet.timestamp.timeIntervalSince(firstTimestamp),
                length: packet.bytes.count,
                changedBytes: byteChanges(previous: comparisonBytes, current: packet.bytes),
                bytes: packet.bytes
            )
        }
    }

    public static func byteChanges(previous: [UInt8]?, current: [UInt8])
        -> [LibUSBBulkPacketByteChange]
    {
        guard let previous else {
            return []
        }

        let maximumLength = max(previous.count, current.count)
        return (0..<maximumLength).compactMap { offset in
            let previousValue = offset < previous.count ? previous[offset] : nil
            let currentValue = offset < current.count ? current[offset] : nil
            guard previousValue != currentValue else {
                return nil
            }

            return LibUSBBulkPacketByteChange(
                offset: offset,
                previousValue: previousValue,
                currentValue: currentValue
            )
        }
    }
}
