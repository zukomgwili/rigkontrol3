import CLibusb
import Foundation

public struct LibUSBDeviceDescriptorSnapshot: Equatable, Sendable {
    public let identity: RigKontrol3DeviceIdentity
    public let busNumber: Int
    public let deviceAddress: Int
    public let speed: String
    public let deviceClass: Int
    public let deviceSubClass: Int
    public let deviceProtocol: Int
    public let configurationCount: Int
    public let openStatus: String
    public let configurations: [LibUSBConfigurationSnapshot]
}

public struct LibUSBConfigurationSnapshot: Equatable, Sendable {
    public let value: Int
    public let attributes: Int
    public let maxPowerMilliamps: Int
    public let interfaces: [LibUSBInterfaceSnapshot]
}

public struct LibUSBInterfaceSnapshot: Equatable, Sendable {
    public let number: Int
    public let alternateSetting: Int
    public let interfaceClass: Int
    public let interfaceSubClass: Int
    public let interfaceProtocol: Int
    public let endpoints: [LibUSBEndpointSnapshot]
}

public struct LibUSBEndpointSnapshot: Equatable, Sendable {
    public let address: Int
    public let attributes: Int
    public let maxPacketSize: Int
    public let interval: Int
    public let direction: String
    public let transferType: String
}

public enum LibUSBProbeError: Error, Equatable {
    case initializationFailed(String)
    case deviceListFailed(String)
}

public enum LibUSBDescriptorProbe {
    public static func rigKontrol3Descriptors() throws -> [LibUSBDeviceDescriptorSnapshot] {
        try allDeviceDescriptors().filter { $0.identity.isRigKontrol3 }
    }

    public static func allDeviceDescriptors() throws -> [LibUSBDeviceDescriptorSnapshot] {
        var context: OpaquePointer?
        let initStatus = libusb_init(&context)
        guard initStatus == success else {
            throw LibUSBProbeError.initializationFailed(errorName(initStatus))
        }
        defer { libusb_exit(context) }

        var deviceList: UnsafeMutablePointer<OpaquePointer?>?
        let deviceCount = libusb_get_device_list(context, &deviceList)
        guard deviceCount >= 0 else {
            throw LibUSBProbeError.deviceListFailed(errorName(Int32(deviceCount)))
        }
        defer { libusb_free_device_list(deviceList, 1) }

        var snapshots: [LibUSBDeviceDescriptorSnapshot] = []
        for index in 0..<Int(deviceCount) {
            guard let device = deviceList?[index] else {
                continue
            }

            var descriptor = libusb_device_descriptor()
            let descriptorStatus = libusb_get_device_descriptor(device, &descriptor)
            guard descriptorStatus == success else {
                continue
            }

            var handle: OpaquePointer?
            let openStatus = libusb_open(device, &handle)
            defer {
                if let handle {
                    libusb_close(handle)
                }
            }

            let identity = RigKontrol3DeviceIdentity(
                vendorID: Int(descriptor.idVendor),
                productID: Int(descriptor.idProduct),
                productName: stringDescriptor(index: descriptor.iProduct, handle: handle),
                manufacturerName: stringDescriptor(index: descriptor.iManufacturer, handle: handle),
                serialNumber: stringDescriptor(index: descriptor.iSerialNumber, handle: handle)
            )

            snapshots.append(
                LibUSBDeviceDescriptorSnapshot(
                    identity: identity,
                    busNumber: Int(libusb_get_bus_number(device)),
                    deviceAddress: Int(libusb_get_device_address(device)),
                    speed: speedName(libusb_get_device_speed(device)),
                    deviceClass: Int(descriptor.bDeviceClass),
                    deviceSubClass: Int(descriptor.bDeviceSubClass),
                    deviceProtocol: Int(descriptor.bDeviceProtocol),
                    configurationCount: Int(descriptor.bNumConfigurations),
                    openStatus: errorName(openStatus),
                    configurations: configurationSnapshots(
                        for: device, count: descriptor.bNumConfigurations)
                )
            )
        }

        return snapshots
    }

    private static func configurationSnapshots(for device: OpaquePointer, count: UInt8)
        -> [LibUSBConfigurationSnapshot]
    {
        var snapshots: [LibUSBConfigurationSnapshot] = []

        for configIndex in 0..<Int(count) {
            var configPointer: UnsafeMutablePointer<libusb_config_descriptor>?
            let status = libusb_get_config_descriptor(device, UInt8(configIndex), &configPointer)
            guard status == success, let configPointer else {
                continue
            }
            defer { libusb_free_config_descriptor(configPointer) }

            let config = configPointer.pointee
            snapshots.append(
                LibUSBConfigurationSnapshot(
                    value: Int(config.bConfigurationValue),
                    attributes: Int(config.bmAttributes),
                    maxPowerMilliamps: Int(config.MaxPower) * 2,
                    interfaces: interfaceSnapshots(config)
                )
            )
        }

        return snapshots
    }

    private static func interfaceSnapshots(_ config: libusb_config_descriptor)
        -> [LibUSBInterfaceSnapshot]
    {
        var snapshots: [LibUSBInterfaceSnapshot] = []

        for interfaceIndex in 0..<Int(config.bNumInterfaces) {
            let interface = config.interface[interfaceIndex]
            for alternateIndex in 0..<Int(interface.num_altsetting) {
                let descriptor = interface.altsetting[alternateIndex]
                snapshots.append(
                    LibUSBInterfaceSnapshot(
                        number: Int(descriptor.bInterfaceNumber),
                        alternateSetting: Int(descriptor.bAlternateSetting),
                        interfaceClass: Int(descriptor.bInterfaceClass),
                        interfaceSubClass: Int(descriptor.bInterfaceSubClass),
                        interfaceProtocol: Int(descriptor.bInterfaceProtocol),
                        endpoints: endpointSnapshots(descriptor)
                    )
                )
            }
        }

        return snapshots
    }

    private static func endpointSnapshots(_ interface: libusb_interface_descriptor)
        -> [LibUSBEndpointSnapshot]
    {
        var snapshots: [LibUSBEndpointSnapshot] = []

        for endpointIndex in 0..<Int(interface.bNumEndpoints) {
            let endpoint = interface.endpoint[endpointIndex]
            snapshots.append(
                LibUSBEndpointSnapshot(
                    address: Int(endpoint.bEndpointAddress),
                    attributes: Int(endpoint.bmAttributes),
                    maxPacketSize: Int(endpoint.wMaxPacketSize),
                    interval: Int(endpoint.bInterval),
                    direction: endpointDirection(endpoint.bEndpointAddress),
                    transferType: transferType(endpoint.bmAttributes)
                )
            )
        }

        return snapshots
    }

    private static func stringDescriptor(index: UInt8, handle: OpaquePointer?) -> String? {
        guard index != 0, let handle else {
            return nil
        }

        var buffer = [UInt8](repeating: 0, count: 256)
        let length = libusb_get_string_descriptor_ascii(handle, index, &buffer, Int32(buffer.count))
        guard length > 0 else {
            return nil
        }

        return String(decoding: buffer.prefix(Int(length)), as: UTF8.self)
    }

    private static func endpointDirection(_ address: UInt8) -> String {
        let inputMask = UInt8(truncatingIfNeeded: UInt32(LIBUSB_ENDPOINT_IN.rawValue))
        return (address & inputMask) == inputMask ? "in" : "out"
    }

    private static func transferType(_ attributes: UInt8) -> String {
        let transferMask = UInt8(truncatingIfNeeded: UInt32(LIBUSB_TRANSFER_TYPE_MASK))
        switch attributes & transferMask {
        case UInt8(truncatingIfNeeded: UInt32(LIBUSB_TRANSFER_TYPE_CONTROL.rawValue)):
            return "control"
        case UInt8(truncatingIfNeeded: UInt32(LIBUSB_TRANSFER_TYPE_ISOCHRONOUS.rawValue)):
            return "isochronous"
        case UInt8(truncatingIfNeeded: UInt32(LIBUSB_TRANSFER_TYPE_BULK.rawValue)):
            return "bulk"
        case UInt8(truncatingIfNeeded: UInt32(LIBUSB_TRANSFER_TYPE_INTERRUPT.rawValue)):
            return "interrupt"
        default:
            return "unknown"
        }
    }

    private static func speedName(_ speed: Int32) -> String {
        switch speed {
        case Int32(LIBUSB_SPEED_LOW.rawValue):
            return "low"
        case Int32(LIBUSB_SPEED_FULL.rawValue):
            return "full"
        case Int32(LIBUSB_SPEED_HIGH.rawValue):
            return "high"
        case Int32(LIBUSB_SPEED_SUPER.rawValue):
            return "super"
        case Int32(LIBUSB_SPEED_SUPER_PLUS.rawValue):
            return "super_plus"
        default:
            return "unknown"
        }
    }

    private static var success: Int32 {
        Int32(LIBUSB_SUCCESS.rawValue)
    }

    private static func errorName(_ status: Int32) -> String {
        guard let pointer = libusb_error_name(status) else {
            return "LIBUSB_ERROR_\(status)"
        }
        return String(cString: pointer)
    }
}
