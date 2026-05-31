import Foundation

public struct RigKontrolLEDState: Equatable, Sendable {
    public static let writeIOCommand: UInt8 = 0x05
    public static let payloadByteCount = 63

    public private(set) var payloadBytes: [UInt8]

    public init(payloadBytes: [UInt8] = Self.defaultPayloadBytes) {
        var normalizedBytes = payloadBytes
        if normalizedBytes.count < Self.payloadByteCount {
            normalizedBytes.append(
                contentsOf: repeatElement(0, count: Self.payloadByteCount - normalizedBytes.count)
            )
        }
        self.payloadBytes = Array(normalizedBytes.prefix(Self.payloadByteCount))
    }

    public static var defaultPayloadBytes: [UInt8] {
        var bytes = [UInt8](repeating: 0, count: payloadByteCount)
        bytes[1] = 0x40
        bytes[2] = 0x40
        return bytes
    }

    public var writeIOCommandBytes: [UInt8] {
        [Self.writeIOCommand] + payloadBytes
    }

    public mutating func setSwitchLED(index: Int, isOn: Bool) {
        precondition((1...9).contains(index), "Rig Kontrol switch LED index must be 1...9")

        let bitPosition = 31 + hardwareLEDIndex(forSwitchIndex: index)
        let byteIndex = bitPosition / 8
        let mask = UInt8(1 << (bitPosition % 8))
        if isOn {
            payloadBytes[byteIndex] |= mask
        } else {
            payloadBytes[byteIndex] &= ~mask
        }
    }

    public func isSwitchLEDOn(index: Int) -> Bool {
        precondition((1...9).contains(index), "Rig Kontrol switch LED index must be 1...9")

        let bitPosition = 31 + hardwareLEDIndex(forSwitchIndex: index)
        let byteIndex = bitPosition / 8
        let mask = UInt8(1 << (bitPosition % 8))
        return (payloadBytes[byteIndex] & mask) != 0
    }

    private func hardwareLEDIndex(forSwitchIndex index: Int) -> Int {
        switch index {
        case 1...4:
            index + 4
        case 5...8:
            index - 4
        default:
            index
        }
    }
}
