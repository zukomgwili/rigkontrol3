import Foundation
import IOKit

public struct USBDeviceSnapshot: Equatable, Sendable {
    public let identity: RigKontrol3DeviceIdentity
    public let registryPath: String?
    public let usbSpeed: Int?
    public let linkSpeed: Int?
    public let deviceClass: Int?
    public let deviceSubClass: Int?
    public let deviceProtocol: Int?
    public let configurationCount: Int?
    public let currentConfiguration: Int?
    public let locationID: Int?
    public let bcdUSB: Int?
    public let bcdDevice: Int?

    public init(
        identity: RigKontrol3DeviceIdentity,
        registryPath: String?,
        usbSpeed: Int?,
        linkSpeed: Int?,
        deviceClass: Int?,
        deviceSubClass: Int?,
        deviceProtocol: Int?,
        configurationCount: Int?,
        currentConfiguration: Int?,
        locationID: Int?,
        bcdUSB: Int?,
        bcdDevice: Int?
    ) {
        self.identity = identity
        self.registryPath = registryPath
        self.usbSpeed = usbSpeed
        self.linkSpeed = linkSpeed
        self.deviceClass = deviceClass
        self.deviceSubClass = deviceSubClass
        self.deviceProtocol = deviceProtocol
        self.configurationCount = configurationCount
        self.currentConfiguration = currentConfiguration
        self.locationID = locationID
        self.bcdUSB = bcdUSB
        self.bcdDevice = bcdDevice
    }
}

public enum USBDeviceProbe {
    public static func rigKontrol3Devices() -> [USBDeviceSnapshot] {
        allUSBHostDevices().filter { $0.identity.isRigKontrol3 }
    }

    public static func allUSBHostDevices() -> [USBDeviceSnapshot] {
        var iterator: io_iterator_t = 0
        let result = IOServiceGetMatchingServices(
            kIOMainPortDefault,
            IOServiceMatching("IOUSBHostDevice"),
            &iterator
        )
        guard result == KERN_SUCCESS else {
            return []
        }
        defer { IOObjectRelease(iterator) }

        var snapshots: [USBDeviceSnapshot] = []
        while true {
            let service = IOIteratorNext(iterator)
            guard service != 0 else { break }
            defer { IOObjectRelease(service) }

            guard let vendorID = numberProperty(service, "idVendor"),
                let productID = numberProperty(service, "idProduct")
            else {
                continue
            }

            let identity = RigKontrol3DeviceIdentity(
                vendorID: vendorID,
                productID: productID,
                productName: stringProperty(service, "USB Product Name")
                    ?? stringProperty(service, "kUSBProductString"),
                manufacturerName: stringProperty(service, "USB Vendor Name")
                    ?? stringProperty(service, "kUSBVendorString"),
                serialNumber: stringProperty(service, "USB Serial Number")
                    ?? stringProperty(service, "kUSBSerialNumberString")
            )

            snapshots.append(
                USBDeviceSnapshot(
                    identity: identity,
                    registryPath: registryPath(for: service),
                    usbSpeed: numberProperty(service, "USBSpeed"),
                    linkSpeed: numberProperty(service, "UsbLinkSpeed"),
                    deviceClass: numberProperty(service, "bDeviceClass"),
                    deviceSubClass: numberProperty(service, "bDeviceSubClass"),
                    deviceProtocol: numberProperty(service, "bDeviceProtocol"),
                    configurationCount: numberProperty(service, "bNumConfigurations"),
                    currentConfiguration: numberProperty(service, "kUSBCurrentConfiguration"),
                    locationID: numberProperty(service, "locationID"),
                    bcdUSB: numberProperty(service, "bcdUSB"),
                    bcdDevice: numberProperty(service, "bcdDevice")
                )
            )
        }
        return snapshots
    }

    private static func property(_ service: io_registry_entry_t, _ key: String) -> Any? {
        IORegistryEntryCreateCFProperty(service, key as CFString, kCFAllocatorDefault, 0)?
            .takeRetainedValue()
    }

    private static func numberProperty(_ service: io_registry_entry_t, _ key: String) -> Int? {
        if let number = property(service, key) as? NSNumber {
            return number.intValue
        }
        return nil
    }

    private static func stringProperty(_ service: io_registry_entry_t, _ key: String) -> String? {
        property(service, key) as? String
    }

    private static func registryPath(for service: io_registry_entry_t) -> String? {
        var path = [CChar](repeating: 0, count: 1024)
        let result = IORegistryEntryGetPath(service, kIOServicePlane, &path)
        guard result == KERN_SUCCESS else {
            return nil
        }
        let bytes = path.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }
        return String(decoding: bytes, as: UTF8.self)
    }
}
