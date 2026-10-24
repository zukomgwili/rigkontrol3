import Foundation

public struct ExpressionCalibration: Equatable, Sendable {
    public var rawMinimum: Int
    public var rawMaximum: Int
    public var outputMinimum: Int
    public var outputMaximum: Int

    public init(
        rawMinimum: Int = 64, rawMaximum: Int = 660, outputMinimum: Int = 0,
        outputMaximum: Int = 127
    ) {
        precondition(rawMaximum > rawMinimum, "rawMaximum must be greater than rawMinimum")
        precondition(
            outputMaximum > outputMinimum, "outputMaximum must be greater than outputMinimum")

        self.rawMinimum = rawMinimum
        self.rawMaximum = rawMaximum
        self.outputMinimum = outputMinimum
        self.outputMaximum = outputMaximum
    }

    /// Keeps a 2% margin at each measured end so minor end-stop noise does not
    /// prevent reaching 0 or 127 on ordinary sweeps. The center remains linear.
    public static func forMeasuredTravel(rawMinimum: Int, rawMaximum: Int)
        -> ExpressionCalibration
    {
        precondition(rawMaximum > rawMinimum, "rawMaximum must be greater than rawMinimum")
        let inset = (rawMaximum - rawMinimum) / 50
        return ExpressionCalibration(
            rawMinimum: rawMinimum + inset, rawMaximum: rawMaximum - inset)
    }

    public func normalized(rawValue: Int) -> Int {
        let clamped = max(rawMinimum, min(rawMaximum, rawValue))
        let inputRange = rawMaximum - rawMinimum
        let outputRange = outputMaximum - outputMinimum
        return outputMinimum + ((clamped - rawMinimum) * outputRange / inputRange)
    }
}

public struct ExpressionValueFilter: Equatable, Sendable {
    public let deadband: Int
    public private(set) var lastAcceptedValue: Int?

    public init(deadband: Int = 1, lastAcceptedValue: Int? = nil) {
        self.deadband = max(0, deadband)
        self.lastAcceptedValue = lastAcceptedValue
    }

    public mutating func accept(_ value: Int) -> Int? {
        guard let lastAcceptedValue else {
            self.lastAcceptedValue = value
            return value
        }

        guard value != lastAcceptedValue,
            value == 0 || value == 127 || abs(value - lastAcceptedValue) > deadband
        else {
            return nil
        }

        self.lastAcceptedValue = value
        return value
    }
}
