import Foundation

public struct RigKontrolGuidedCaptureChangeRecord: Codable, Equatable, Sendable {
    public let offset: Int
    public let previousValue: UInt8?
    public let previousHex: String?
    public let currentValue: UInt8?
    public let currentHex: String?

    public init(offset: Int, previousValue: UInt8?, currentValue: UInt8?) {
        self.offset = offset
        self.previousValue = previousValue
        self.previousHex = previousValue.map(Self.hexByte)
        self.currentValue = currentValue
        self.currentHex = currentValue.map(Self.hexByte)
    }

    private static func hexByte(_ byte: UInt8) -> String {
        let value = String(byte, radix: 16, uppercase: false)
        return value.count == 1 ? "0" + value : value
    }
}

public struct RigKontrolGuidedCaptureRecord: Codable, Equatable, Sendable {
    public let schemaVersion: Int
    public let stepIndex: Int
    public let stepNumber: Int
    public let stepCount: Int
    public let stepLabel: String
    public let stepInstruction: String
    public let packetIndex: Int
    public let packetNumber: Int
    public let timestampSecondsSince1970: TimeInterval
    public let relativeTime: TimeInterval
    public let endpointAddress: Int
    public let length: Int
    public let bytes: [UInt8]
    public let bytesHex: String
    public let changes: [RigKontrolGuidedCaptureChangeRecord]

    public init(
        schemaVersion: Int = 1,
        stepIndex: Int,
        stepCount: Int,
        step: RigKontrolGuidedCaptureStep,
        observation: LibUSBBulkPacketObservation
    ) {
        self.schemaVersion = schemaVersion
        self.stepIndex = stepIndex
        self.stepNumber = stepIndex + 1
        self.stepCount = stepCount
        self.stepLabel = step.label
        self.stepInstruction = step.instruction
        self.packetIndex = observation.packetIndex
        self.packetNumber = observation.packetIndex + 1
        self.timestampSecondsSince1970 = observation.timestamp.timeIntervalSince1970
        self.relativeTime = observation.relativeTime
        self.endpointAddress = observation.endpointAddress
        self.length = observation.length
        self.bytes = observation.bytes
        self.bytesHex = Self.hexBytes(observation.bytes)
        self.changes = observation.changedBytes.map { change in
            RigKontrolGuidedCaptureChangeRecord(
                offset: change.offset,
                previousValue: change.previousValue,
                currentValue: change.currentValue
            )
        }
    }

    public init(
        schemaVersion: Int = 1,
        stepIndex: Int,
        stepCount: Int,
        stepLabel: String,
        stepInstruction: String,
        packetIndex: Int,
        packetNumber: Int,
        timestampSecondsSince1970: TimeInterval,
        relativeTime: TimeInterval,
        endpointAddress: Int,
        length: Int,
        bytes: [UInt8],
        changes: [RigKontrolGuidedCaptureChangeRecord]
    ) {
        self.schemaVersion = schemaVersion
        self.stepIndex = stepIndex
        self.stepNumber = stepIndex + 1
        self.stepCount = stepCount
        self.stepLabel = stepLabel
        self.stepInstruction = stepInstruction
        self.packetIndex = packetIndex
        self.packetNumber = packetNumber
        self.timestampSecondsSince1970 = timestampSecondsSince1970
        self.relativeTime = relativeTime
        self.endpointAddress = endpointAddress
        self.length = length
        self.bytes = bytes
        self.bytesHex = Self.hexBytes(bytes)
        self.changes = changes
    }

    private static func hexBytes(_ bytes: [UInt8]) -> String {
        bytes.map { byte in
            let value = String(byte, radix: 16, uppercase: false)
            return value.count == 1 ? "0" + value : value
        }.joined(separator: " ")
    }
}
