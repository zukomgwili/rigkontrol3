import Foundation

public struct RigKontrolPacketParser: Equatable, Sendable {
    public static let switchReportID: UInt8 = 0x04
    public static let expressionReportID: UInt8 = 0x03

    public static let verifiedSwitchBitMap: [UInt8: Int] = [
        0x10: 1,
        0x20: 2,
        0x40: 3,
        0x80: 4,
        0x01: 5,
        0x02: 6,
        0x04: 7,
        0x08: 8,
    ]

    public var switchBitMap: [UInt8: Int]
    public var expressionCalibration: ExpressionCalibration
    public var expressionFilter: ExpressionValueFilter
    public private(set) var previousSwitchMask: UInt8?

    public init(
        switchBitMap: [UInt8: Int] = Self.verifiedSwitchBitMap,
        expressionCalibration: ExpressionCalibration = ExpressionCalibration(),
        expressionFilter: ExpressionValueFilter = ExpressionValueFilter(),
        previousSwitchMask: UInt8? = nil
    ) {
        self.switchBitMap = switchBitMap
        self.expressionCalibration = expressionCalibration
        self.expressionFilter = expressionFilter
        self.previousSwitchMask = previousSwitchMask
    }

    public mutating func events(for packet: [UInt8]) -> [RigKontrolControlEvent] {
        if let switchMask = Self.switchMask(from: packet) {
            return switchEvents(for: switchMask)
        }

        if let rawExpressionValue = Self.rawExpressionValue(from: packet) {
            let normalizedValue = expressionCalibration.normalized(rawValue: rawExpressionValue)
            return expressionFilter.accept(normalizedValue).map {
                [.expressionChanged(normalizedValue: $0)]
            } ?? []
        }

        return []
    }

    public static func switchMask(from packet: [UInt8]) -> UInt8? {
        guard packet.count >= 2, packet[0] == switchReportID else {
            return nil
        }

        return packet[1]
    }

    public static func rawExpressionValue(from packet: [UInt8]) -> Int? {
        guard packet.count >= 7, packet[0] == expressionReportID else {
            return nil
        }

        return (Int(packet[5]) << 8) | Int(packet[6])
    }

    private mutating func switchEvents(for currentSwitchMask: UInt8)
        -> [RigKontrolControlEvent]
    {
        let previousSwitchMask = previousSwitchMask ?? 0
        self.previousSwitchMask = currentSwitchMask

        return switchBitMap.sorted { lhs, rhs in
            lhs.value < rhs.value
        }.compactMap { bit, switchIndex in
            let wasPressed = (previousSwitchMask & bit) != 0
            let isPressed = (currentSwitchMask & bit) != 0
            guard wasPressed != isPressed else {
                return nil
            }

            return .switchChanged(index: switchIndex, isPressed: isPressed)
        }
    }
}
