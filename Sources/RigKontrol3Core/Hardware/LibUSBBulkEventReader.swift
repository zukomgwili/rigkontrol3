import CLibusb
import Foundation

// Compatibility names for the legacy CLI and diagnostic probes.
public typealias LibUSBBulkReadPacket = RigKontrolUSBReadPacket
public struct LibUSBBulkReadSummary: Sendable {
    public let openStatus: String
    public let configurationStatus: String?
    public let claimStatus: String?
    public let alternateSettingStatus: String?
    public let releaseStatus: String?
    public let packets: [LibUSBBulkReadPacket]
    public let nonTimeoutErrors: [String]

    public init(
        openStatus: String,
        configurationStatus: String?,
        claimStatus: String?,
        alternateSettingStatus: String?,
        releaseStatus: String?,
        packets: [LibUSBBulkReadPacket],
        nonTimeoutErrors: [String]
    ) {
        self.openStatus = openStatus
        self.configurationStatus = configurationStatus
        self.claimStatus = claimStatus
        self.alternateSettingStatus = alternateSettingStatus
        self.releaseStatus = releaseStatus
        self.packets = packets
        self.nonTimeoutErrors = nonTimeoutErrors
    }
}

public typealias LibUSBBulkStreamSummary = RigKontrolUSBStreamSummary
public typealias LibUSBBulkWriteResult = RigKontrolUSBWriteResult
public typealias LibUSBBulkOutputWriter = RigKontrolUSBOutputWriter
public typealias LibUSBBulkReadError = RigKontrolUSBReadError

public enum LibUSBBulkEventReader {
    public static func streamRigKontrol3BulkInput(
        interfaceNumber: Int32 = 0,
        alternateSetting: Int32 = 0,
        endpointAddress: UInt8 = 0x81,
        outputEndpointAddress: UInt8 = 0x01,
        packetSize: Int = 512,
        timeoutMilliseconds: UInt32 = 100,
        didOpen: ((LibUSBBulkOutputWriter) -> Void)? = nil,
        willClose: ((LibUSBBulkOutputWriter) -> Void)? = nil,
        shouldContinue: () -> Bool,
        packetHandler: (LibUSBBulkReadPacket, LibUSBBulkOutputWriter) -> Void
    ) throws -> LibUSBBulkStreamSummary {
        var context: OpaquePointer?
        let initStatus = libusb_init(&context)
        guard initStatus == success else {
            throw LibUSBBulkReadError.initializationFailed(errorName(initStatus))
        }
        defer { libusb_exit(context) }

        var deviceList: UnsafeMutablePointer<OpaquePointer?>?
        let deviceCount = libusb_get_device_list(context, &deviceList)
        guard deviceCount >= 0 else {
            throw LibUSBBulkReadError.deviceListFailed(errorName(Int32(deviceCount)))
        }
        defer { libusb_free_device_list(deviceList, 1) }

        guard let device = firstRigKontrol3Device(in: deviceList, count: Int(deviceCount)) else {
            throw LibUSBBulkReadError.deviceNotFound
        }

        var handle: OpaquePointer?
        let openStatus = libusb_open(device, &handle)
        guard openStatus == success, let handle else {
            return LibUSBBulkStreamSummary(
                openStatus: errorName(openStatus),
                configurationStatus: nil,
                claimStatus: nil,
                alternateSettingStatus: nil,
                releaseStatus: nil,
                packetCount: 0,
                nonTimeoutErrors: []
            )
        }
        defer { libusb_close(handle) }

        var currentConfiguration: Int32 = 0
        var configurationStatus = libusb_get_configuration(handle, &currentConfiguration)
        if configurationStatus == success, currentConfiguration != 1 {
            configurationStatus = libusb_set_configuration(handle, 1)
        }
        guard configurationStatus == success else {
            return LibUSBBulkStreamSummary(
                openStatus: errorName(openStatus),
                configurationStatus: errorName(configurationStatus),
                claimStatus: nil,
                alternateSettingStatus: nil,
                releaseStatus: nil,
                packetCount: 0,
                nonTimeoutErrors: []
            )
        }
        let claimStatus = libusb_claim_interface(handle, interfaceNumber)
        guard claimStatus == success else {
            return LibUSBBulkStreamSummary(
                openStatus: errorName(openStatus),
                configurationStatus: errorName(configurationStatus),
                claimStatus: errorName(claimStatus),
                alternateSettingStatus: nil,
                releaseStatus: nil,
                packetCount: 0,
                nonTimeoutErrors: []
            )
        }

        let alternateSettingStatus = libusb_set_interface_alt_setting(
            handle, interfaceNumber, alternateSetting)
        guard alternateSettingStatus == success else {
            let releaseStatus = libusb_release_interface(handle, interfaceNumber)
            return LibUSBBulkStreamSummary(
                openStatus: errorName(openStatus),
                configurationStatus: errorName(configurationStatus),
                claimStatus: errorName(claimStatus),
                alternateSettingStatus: errorName(alternateSettingStatus),
                releaseStatus: errorName(releaseStatus),
                packetCount: 0,
                nonTimeoutErrors: []
            )
        }

        let outputWriter = LibUSBBulkOutputWriter(endpointAddress: Int(outputEndpointAddress)) {
            bytes, timeoutMilliseconds in
            guard !bytes.isEmpty else {
                return LibUSBBulkWriteResult(status: errorName(success), transferred: 0)
            }

            var buffer = bytes
            let byteCount = buffer.count
            var transferred: Int32 = 0
            let writeStatus = buffer.withUnsafeMutableBufferPointer { pointer in
                libusb_bulk_transfer(
                    handle,
                    outputEndpointAddress,
                    pointer.baseAddress,
                    Int32(byteCount),
                    &transferred,
                    timeoutMilliseconds
                )
            }

            return LibUSBBulkWriteResult(
                status: errorName(writeStatus),
                transferred: Int(transferred)
            )
        }

        // Attached-unit regression on 2026-10-24: undrained EP1-IN traffic can
        // block OUT commands, including between consecutive startup writes.
        // Keep input reads active through startup, operation and LED cleanup.
        // The pump never calls client code or writes to the device.
        let inputPump = LibUSBBulkInputPump(
            handle: handle, endpointAddress: endpointAddress, packetSize: packetSize,
            timeoutMilliseconds: min(100, max(1, timeoutMilliseconds)))
        if let startupError = inputPump.start() {
            inputPump.stopAndWait()
            let releaseStatus = libusb_release_interface(handle, interfaceNumber)
            return LibUSBBulkStreamSummary(
                openStatus: errorName(openStatus),
                configurationStatus: errorName(configurationStatus),
                claimStatus: errorName(claimStatus),
                alternateSettingStatus: errorName(alternateSettingStatus),
                releaseStatus: errorName(releaseStatus),
                packetCount: 0,
                nonTimeoutErrors: [startupError]
            )
        }

        didOpen?(outputWriter)

        var packetCount = 0
        var nonTimeoutErrors: [String] = []

        while shouldContinue() {
            let packet = inputPump.nextPacket()
            if let error = inputPump.failure {
                nonTimeoutErrors.append(error)
                break
            }
            if let packet, shouldContinue() {
                packetCount += 1
                packetHandler(packet, outputWriter)
            }
        }

        willClose?(outputWriter)
        inputPump.stopAndWait()
        if let error = inputPump.failure, !nonTimeoutErrors.contains(error) {
            nonTimeoutErrors.append(error)
        }

        let finalReleaseStatus = libusb_release_interface(handle, interfaceNumber)

        return LibUSBBulkStreamSummary(
            openStatus: errorName(openStatus),
            configurationStatus: errorName(configurationStatus),
            claimStatus: errorName(claimStatus),
            alternateSettingStatus: errorName(alternateSettingStatus),
            releaseStatus: errorName(finalReleaseStatus),
            packetCount: packetCount,
            nonTimeoutErrors: nonTimeoutErrors
        )
    }

    public static func readRigKontrol3BulkInput(
        duration: TimeInterval,
        interfaceNumber: Int32 = 0,
        alternateSetting: Int32 = 0,
        endpointAddress: UInt8 = 0x81,
        packetSize: Int = 512,
        timeoutMilliseconds: UInt32 = 100
    ) throws -> LibUSBBulkReadSummary {
        var context: OpaquePointer?
        let initStatus = libusb_init(&context)
        guard initStatus == success else {
            throw LibUSBBulkReadError.initializationFailed(errorName(initStatus))
        }
        defer { libusb_exit(context) }

        var deviceList: UnsafeMutablePointer<OpaquePointer?>?
        let deviceCount = libusb_get_device_list(context, &deviceList)
        guard deviceCount >= 0 else {
            throw LibUSBBulkReadError.deviceListFailed(errorName(Int32(deviceCount)))
        }
        defer { libusb_free_device_list(deviceList, 1) }

        guard let device = firstRigKontrol3Device(in: deviceList, count: Int(deviceCount)) else {
            throw LibUSBBulkReadError.deviceNotFound
        }

        var handle: OpaquePointer?
        let openStatus = libusb_open(device, &handle)
        guard openStatus == success, let handle else {
            return LibUSBBulkReadSummary(
                openStatus: errorName(openStatus),
                configurationStatus: nil,
                claimStatus: nil,
                alternateSettingStatus: nil,
                releaseStatus: nil,
                packets: [],
                nonTimeoutErrors: []
            )
        }
        defer { libusb_close(handle) }

        let configurationStatus = libusb_set_configuration(handle, 1)
        let claimStatus = libusb_claim_interface(handle, interfaceNumber)
        guard claimStatus == success else {
            return LibUSBBulkReadSummary(
                openStatus: errorName(openStatus),
                configurationStatus: errorName(configurationStatus),
                claimStatus: errorName(claimStatus),
                alternateSettingStatus: nil,
                releaseStatus: nil,
                packets: [],
                nonTimeoutErrors: []
            )
        }

        let alternateSettingStatus = libusb_set_interface_alt_setting(
            handle, interfaceNumber, alternateSetting)
        guard alternateSettingStatus == success else {
            let releaseStatus = libusb_release_interface(handle, interfaceNumber)
            return LibUSBBulkReadSummary(
                openStatus: errorName(openStatus),
                configurationStatus: errorName(configurationStatus),
                claimStatus: errorName(claimStatus),
                alternateSettingStatus: errorName(alternateSettingStatus),
                releaseStatus: errorName(releaseStatus),
                packets: [],
                nonTimeoutErrors: []
            )
        }

        var packets: [LibUSBBulkReadPacket] = []
        var nonTimeoutErrors: [String] = []
        let deadline = Date().addingTimeInterval(duration)

        while Date() < deadline {
            var buffer = [UInt8](repeating: 0, count: packetSize)
            var transferred: Int32 = 0
            let readStatus = buffer.withUnsafeMutableBufferPointer { pointer in
                libusb_bulk_transfer(
                    handle,
                    endpointAddress,
                    pointer.baseAddress,
                    Int32(packetSize),
                    &transferred,
                    timeoutMilliseconds
                )
            }

            if readStatus == success, transferred > 0 {
                packets.append(
                    LibUSBBulkReadPacket(
                        endpointAddress: Int(endpointAddress),
                        timestamp: Date(),
                        bytes: Array(buffer.prefix(Int(transferred)))
                    )
                )
            } else if readStatus != timeout {
                nonTimeoutErrors.append(errorName(readStatus))
                break
            }
        }

        let finalReleaseStatus = libusb_release_interface(handle, interfaceNumber)

        return LibUSBBulkReadSummary(
            openStatus: errorName(openStatus),
            configurationStatus: errorName(configurationStatus),
            claimStatus: errorName(claimStatus),
            alternateSettingStatus: errorName(alternateSettingStatus),
            releaseStatus: errorName(finalReleaseStatus),
            packets: packets,
            nonTimeoutErrors: nonTimeoutErrors
        )
    }

    private static func firstRigKontrol3Device(
        in deviceList: UnsafeMutablePointer<OpaquePointer?>?, count: Int
    ) -> OpaquePointer? {
        for index in 0..<count {
            guard let device = deviceList?[index] else {
                continue
            }

            var descriptor = libusb_device_descriptor()
            guard libusb_get_device_descriptor(device, &descriptor) == success else {
                continue
            }

            if Int(descriptor.idVendor) == RigKontrol3DeviceIdentity.vendorID,
                Int(descriptor.idProduct) == RigKontrol3DeviceIdentity.productID
            {
                return device
            }
        }
        return nil
    }

    private static var success: Int32 {
        Int32(LIBUSB_SUCCESS.rawValue)
    }

    private static var timeout: Int32 {
        Int32(LIBUSB_ERROR_TIMEOUT.rawValue)
    }

    private static func errorName(_ status: Int32) -> String {
        guard let pointer = libusb_error_name(status) else {
            return "LIBUSB_ERROR_\(status)"
        }
        return String(cString: pointer)
    }
}

// Only the legacy adapter knows how to turn a libusb handle into pump reads.
extension RigKontrolUSBInputPump {
    convenience init(
        handle: OpaquePointer, endpointAddress: UInt8, packetSize: Int,
        timeoutMilliseconds: UInt32
    ) {
        self.init {
            var buffer = [UInt8](repeating: 0, count: packetSize)
            var transferred: Int32 = 0
            let status = buffer.withUnsafeMutableBufferPointer { pointer in
                libusb_bulk_transfer(
                    handle, endpointAddress, pointer.baseAddress, Int32(packetSize),
                    &transferred, timeoutMilliseconds)
            }
            if status == Int32(LIBUSB_SUCCESS.rawValue) {
                guard transferred > 0 else { return .idle }
                return .packet(LibUSBBulkReadPacket(
                    endpointAddress: Int(endpointAddress), timestamp: Date(),
                    bytes: Array(buffer.prefix(Int(transferred)))))
            }
            if status == Int32(LIBUSB_ERROR_TIMEOUT.rawValue) { return .idle }
            let name = libusb_error_name(status).map { String(cString: $0) }
                ?? "LIBUSB_ERROR_\(status)"
            return .failed(name)
        }
    }

}
