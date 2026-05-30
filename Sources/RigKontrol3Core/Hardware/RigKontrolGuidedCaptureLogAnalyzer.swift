public struct RigKontrolGuidedCaptureTransitionSummary: Equatable, Sendable {
    public let previousValue: UInt8?
    public let previousHex: String?
    public let currentValue: UInt8?
    public let currentHex: String?
    public let count: Int

    public init(previousValue: UInt8?, currentValue: UInt8?, count: Int) {
        self.previousValue = previousValue
        self.previousHex = previousValue.map(Self.hexByte)
        self.currentValue = currentValue
        self.currentHex = currentValue.map(Self.hexByte)
        self.count = count
    }

    private static func hexByte(_ byte: UInt8) -> String {
        let value = String(byte, radix: 16, uppercase: false)
        return value.count == 1 ? "0" + value : value
    }
}

public struct RigKontrolGuidedCaptureOffsetSummary: Equatable, Sendable {
    public let offset: Int
    public let changeCount: Int
    public let transitions: [RigKontrolGuidedCaptureTransitionSummary]

    public init(
        offset: Int,
        changeCount: Int,
        transitions: [RigKontrolGuidedCaptureTransitionSummary]
    ) {
        self.offset = offset
        self.changeCount = changeCount
        self.transitions = transitions
    }
}

public struct RigKontrolGuidedCaptureStepSummary: Equatable, Sendable {
    public let stepIndex: Int
    public let stepNumber: Int
    public let stepCount: Int
    public let stepLabel: String
    public let stepInstruction: String
    public let packetCount: Int
    public let changedPacketCount: Int
    public let switchReportCount: Int
    public let switchReportMasks: [UInt8]
    public let expressionReportCount: Int
    public let expressionRawMinimum: Int?
    public let expressionRawMaximum: Int?
    public let candidateOffsets: [Int]
    public let offsets: [RigKontrolGuidedCaptureOffsetSummary]

    public init(
        stepIndex: Int,
        stepNumber: Int,
        stepCount: Int,
        stepLabel: String,
        stepInstruction: String,
        packetCount: Int,
        changedPacketCount: Int,
        switchReportCount: Int = 0,
        switchReportMasks: [UInt8] = [],
        expressionReportCount: Int = 0,
        expressionRawMinimum: Int? = nil,
        expressionRawMaximum: Int? = nil,
        candidateOffsets: [Int] = [],
        offsets: [RigKontrolGuidedCaptureOffsetSummary]
    ) {
        self.stepIndex = stepIndex
        self.stepNumber = stepNumber
        self.stepCount = stepCount
        self.stepLabel = stepLabel
        self.stepInstruction = stepInstruction
        self.packetCount = packetCount
        self.changedPacketCount = changedPacketCount
        self.switchReportCount = switchReportCount
        self.switchReportMasks = switchReportMasks
        self.expressionReportCount = expressionReportCount
        self.expressionRawMinimum = expressionRawMinimum
        self.expressionRawMaximum = expressionRawMaximum
        self.candidateOffsets = candidateOffsets
        self.offsets = offsets
    }
}

public struct RigKontrolGuidedCaptureLogSummary: Equatable, Sendable {
    public let recordCount: Int
    public let stepSummaries: [RigKontrolGuidedCaptureStepSummary]
    public let baselineChangingOffsets: [Int]
    public let missingSteps: [RigKontrolGuidedCaptureMissingStepSummary]

    public init(
        recordCount: Int,
        stepSummaries: [RigKontrolGuidedCaptureStepSummary],
        baselineChangingOffsets: [Int] = [],
        missingSteps: [RigKontrolGuidedCaptureMissingStepSummary] = []
    ) {
        self.recordCount = recordCount
        self.stepSummaries = stepSummaries
        self.baselineChangingOffsets = baselineChangingOffsets
        self.missingSteps = missingSteps
    }
}

public struct RigKontrolGuidedCaptureMissingStepSummary: Equatable, Sendable {
    public let stepIndex: Int
    public let stepNumber: Int
    public let stepCount: Int
    public let stepLabel: String
    public let stepInstruction: String

    public init(
        stepIndex: Int,
        stepCount: Int,
        step: RigKontrolGuidedCaptureStep
    ) {
        self.stepIndex = stepIndex
        self.stepNumber = stepIndex + 1
        self.stepCount = stepCount
        self.stepLabel = step.label
        self.stepInstruction = step.instruction
    }
}

public enum RigKontrolGuidedCaptureLogAnalyzer {
    public static func summarize(
        _ records: [RigKontrolGuidedCaptureRecord],
        expectedSteps: [RigKontrolGuidedCaptureStep] = RigKontrolGuidedCapturePlan.defaultSteps
    )
        -> RigKontrolGuidedCaptureLogSummary
    {
        let groupedRecords = Dictionary(grouping: records, by: \.stepIndex)
        let baselineChangingOffsets = baselineChangingOffsets(
            from: groupedRecords[0] ?? []
        )
        let stepSummaries = groupedRecords.keys.sorted().compactMap {
            stepIndex -> RigKontrolGuidedCaptureStepSummary? in
            guard let stepRecords = groupedRecords[stepIndex], let firstRecord = stepRecords.first
            else {
                return nil
            }
            let offsets = offsetSummaries(for: stepRecords)
            let switchReportMasks = switchReportMasks(for: stepRecords)
            let expressionRawValues = expressionRawValues(for: stepRecords)

            return RigKontrolGuidedCaptureStepSummary(
                stepIndex: stepIndex,
                stepNumber: firstRecord.stepNumber,
                stepCount: firstRecord.stepCount,
                stepLabel: firstRecord.stepLabel,
                stepInstruction: firstRecord.stepInstruction,
                packetCount: stepRecords.count,
                changedPacketCount: stepRecords.filter { !$0.changes.isEmpty }.count,
                switchReportCount: switchReportCount(for: stepRecords),
                switchReportMasks: switchReportMasks,
                expressionReportCount: expressionRawValues.count,
                expressionRawMinimum: expressionRawValues.min(),
                expressionRawMaximum: expressionRawValues.max(),
                candidateOffsets: candidateOffsets(
                    from: offsets,
                    stepIndex: stepIndex
                ),
                offsets: offsets
            )
        }

        return RigKontrolGuidedCaptureLogSummary(
            recordCount: records.count,
            stepSummaries: stepSummaries,
            baselineChangingOffsets: baselineChangingOffsets,
            missingSteps: missingStepSummaries(
                expectedSteps: expectedSteps,
                recordedStepIndexes: Set(groupedRecords.keys)
            )
        )
    }

    private static func switchReportCount(for records: [RigKontrolGuidedCaptureRecord]) -> Int {
        records.filter { RigKontrolPacketParser.switchMask(from: $0.bytes) != nil }.count
    }

    private static func switchReportMasks(for records: [RigKontrolGuidedCaptureRecord]) -> [UInt8] {
        Array(
            Set(
                records.compactMap { record in
                    RigKontrolPacketParser.switchMask(from: record.bytes)
                })
        ).sorted()
    }

    private static func expressionRawValues(for records: [RigKontrolGuidedCaptureRecord]) -> [Int] {
        records.compactMap { record in
            RigKontrolPacketParser.rawExpressionValue(from: record.bytes)
        }
    }

    private static func baselineChangingOffsets(
        from records: [RigKontrolGuidedCaptureRecord]
    ) -> [Int] {
        let changingOffsets = records.flatMap(\.changes).compactMap { change -> Int? in
            guard change.previousValue != nil, change.currentValue != nil else {
                return nil
            }

            return change.offset
        }

        return Array(Set(changingOffsets)).sorted()
    }

    private static func candidateOffsets(
        from offsets: [RigKontrolGuidedCaptureOffsetSummary],
        stepIndex: Int
    ) -> [Int] {
        guard stepIndex != 0 else {
            return []
        }

        return offsets.compactMap { offsetSummary in
            guard offsetSummary.offset != 0,
                offsetSummary.transitions.contains(where: { transition in
                    transition.previousValue != nil && transition.currentValue != nil
                })
            else {
                return nil
            }

            return offsetSummary.offset
        }
    }

    private static func missingStepSummaries(
        expectedSteps: [RigKontrolGuidedCaptureStep],
        recordedStepIndexes: Set<Int>
    ) -> [RigKontrolGuidedCaptureMissingStepSummary] {
        expectedSteps.enumerated().compactMap { index, step in
            guard !recordedStepIndexes.contains(index) else {
                return nil
            }

            return RigKontrolGuidedCaptureMissingStepSummary(
                stepIndex: index,
                stepCount: expectedSteps.count,
                step: step
            )
        }
    }

    private static func offsetSummaries(for records: [RigKontrolGuidedCaptureRecord])
        -> [RigKontrolGuidedCaptureOffsetSummary]
    {
        let changes = records.flatMap(\.changes)
        let groupedChanges = Dictionary(grouping: changes, by: \.offset)

        return groupedChanges.keys.sorted().map { offset in
            let offsetChanges = groupedChanges[offset] ?? []
            return RigKontrolGuidedCaptureOffsetSummary(
                offset: offset,
                changeCount: offsetChanges.count,
                transitions: transitionSummaries(for: offsetChanges)
            )
        }
    }

    private static func transitionSummaries(for changes: [RigKontrolGuidedCaptureChangeRecord])
        -> [RigKontrolGuidedCaptureTransitionSummary]
    {
        let groupedChanges = Dictionary(grouping: changes) { change in
            TransitionKey(previousValue: change.previousValue, currentValue: change.currentValue)
        }

        return groupedChanges.keys.sorted().map { key in
            RigKontrolGuidedCaptureTransitionSummary(
                previousValue: key.previousValue,
                currentValue: key.currentValue,
                count: groupedChanges[key]?.count ?? 0
            )
        }
    }

    private struct TransitionKey: Comparable, Hashable {
        let previousValue: UInt8?
        let currentValue: UInt8?

        static func < (lhs: TransitionKey, rhs: TransitionKey) -> Bool {
            if sortValue(lhs.previousValue) != sortValue(rhs.previousValue) {
                return sortValue(lhs.previousValue) < sortValue(rhs.previousValue)
            }

            return sortValue(lhs.currentValue) < sortValue(rhs.currentValue)
        }

        private static func sortValue(_ value: UInt8?) -> Int {
            value.map(Int.init) ?? -1
        }
    }
}
