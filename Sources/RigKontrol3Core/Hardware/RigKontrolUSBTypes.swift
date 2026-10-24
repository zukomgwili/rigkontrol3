import Foundation

/// Transport-neutral controller packets retain their acquisition time across buffering.
public struct RigKontrolUSBReadPacket: Sendable {
    public let endpointAddress: Int
    public let timestamp: Date
    public let bytes: [UInt8]

    public init(endpointAddress: Int, timestamp: Date, bytes: [UInt8]) {
        self.endpointAddress = endpointAddress
        self.timestamp = timestamp
        self.bytes = bytes
    }
}

public struct RigKontrolUSBStreamSummary: Sendable {
    public let openStatus: String
    public let configurationStatus: String?
    public let claimStatus: String?
    public let alternateSettingStatus: String?
    public let releaseStatus: String?
    public let packetCount: Int
    public let nonTimeoutErrors: [String]

    public init(
        openStatus: String, configurationStatus: String?, claimStatus: String?,
        alternateSettingStatus: String?, releaseStatus: String?, packetCount: Int,
        nonTimeoutErrors: [String]
    ) {
        self.openStatus = openStatus
        self.configurationStatus = configurationStatus
        self.claimStatus = claimStatus
        self.alternateSettingStatus = alternateSettingStatus
        self.releaseStatus = releaseStatus
        self.packetCount = packetCount
        self.nonTimeoutErrors = nonTimeoutErrors
    }
}

public struct RigKontrolUSBWriteResult: Equatable, Sendable {
    public let status: String
    public let transferred: Int

    public init(status: String, transferred: Int) {
        self.status = status
        self.transferred = transferred
    }
}

/// Used synchronously by the connection owner; never retained past `willClose`.
public struct RigKontrolUSBOutputWriter {
    public let endpointAddress: Int
    private let writeBytes: ([UInt8], UInt32) -> RigKontrolUSBWriteResult

    init(endpointAddress: Int, writeBytes: @escaping ([UInt8], UInt32) -> RigKontrolUSBWriteResult) {
        self.endpointAddress = endpointAddress
        self.writeBytes = writeBytes
    }

    public func write(_ bytes: [UInt8], timeoutMilliseconds: UInt32 = 200) -> RigKontrolUSBWriteResult {
        writeBytes(bytes, timeoutMilliseconds)
    }
}

public enum RigKontrolUSBReadError: Error, Equatable {
    case initializationFailed(String)
    case deviceListFailed(String)
    case deviceNotFound
    case operationFailed(String)
}
