import Foundation
import IOKit.hid

public struct HIDDeviceSnapshot: Equatable, Sendable {
    public let identity: RigKontrol3DeviceIdentity
    public let primaryUsagePage: Int?
    public let primaryUsage: Int?

    public init(identity: RigKontrol3DeviceIdentity, primaryUsagePage: Int?, primaryUsage: Int?) {
        self.identity = identity
        self.primaryUsagePage = primaryUsagePage
        self.primaryUsage = primaryUsage
    }
}

public enum HIDDeviceProbe {
    public static func rigKontrol3Devices() -> [HIDDeviceSnapshot] {
        let manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
        let matching: [String: Any] = [
            kIOHIDVendorIDKey: RigKontrol3DeviceIdentity.vendorID,
            kIOHIDProductIDKey: RigKontrol3DeviceIdentity.productID,
        ]
        IOHIDManagerSetDeviceMatching(manager, matching as CFDictionary)

        let openResult = IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone))
        guard openResult == kIOReturnSuccess else {
            return []
        }
        defer { IOHIDManagerClose(manager, IOOptionBits(kIOHIDOptionsTypeNone)) }

        guard let devices = IOHIDManagerCopyDevices(manager) as? Set<IOHIDDevice> else {
            return []
        }

        return devices.compactMap { device in
            guard let vendorID = numberProperty(device, kIOHIDVendorIDKey),
                let productID = numberProperty(device, kIOHIDProductIDKey)
            else {
                return nil
            }

            let identity = RigKontrol3DeviceIdentity(
                vendorID: vendorID,
                productID: productID,
                productName: stringProperty(device, kIOHIDProductKey),
                manufacturerName: stringProperty(device, kIOHIDManufacturerKey),
                serialNumber: stringProperty(device, kIOHIDSerialNumberKey)
            )

            return HIDDeviceSnapshot(
                identity: identity,
                primaryUsagePage: numberProperty(device, kIOHIDPrimaryUsagePageKey),
                primaryUsage: numberProperty(device, kIOHIDPrimaryUsageKey)
            )
        }
    }

    private static func numberProperty(_ device: IOHIDDevice, _ key: String) -> Int? {
        IOHIDDeviceGetProperty(device, key as CFString) as? Int
    }

    private static func stringProperty(_ device: IOHIDDevice, _ key: String) -> String? {
        IOHIDDeviceGetProperty(device, key as CFString) as? String
    }
}
