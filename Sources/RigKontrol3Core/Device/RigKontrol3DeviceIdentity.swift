import Foundation

public struct RigKontrol3DeviceIdentity: Equatable, Sendable {
    public static let vendorID = 0x17cc
    public static let productID = 0x1940

    public let vendorID: Int
    public let productID: Int
    public let productName: String?
    public let manufacturerName: String?
    public let serialNumber: String?

    public init(
        vendorID: Int,
        productID: Int,
        productName: String? = nil,
        manufacturerName: String? = nil,
        serialNumber: String? = nil
    ) {
        self.vendorID = vendorID
        self.productID = productID
        self.productName = productName
        self.manufacturerName = manufacturerName
        self.serialNumber = serialNumber?.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    public var isRigKontrol3: Bool {
        vendorID == Self.vendorID && productID == Self.productID
    }
}
