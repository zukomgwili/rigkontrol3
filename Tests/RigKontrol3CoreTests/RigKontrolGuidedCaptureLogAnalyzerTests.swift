import RigKontrol3Core
import XCTest

final class RigKontrolGuidedCaptureLogAnalyzerTests: XCTestCase {
    func testEmptyLogProducesEmptySummary() {
        let summary = RigKontrolGuidedCaptureLogAnalyzer.summarize([])

        XCTAssertEqual(summary.recordCount, 0)
        XCTAssertEqual(summary.stepSummaries, [])
        XCTAssertEqual(summary.baselineChangingOffsets, [])
        XCTAssertEqual(summary.missingSteps.count, RigKontrolGuidedCapturePlan.defaultSteps.count)
    }

    func testSummaryGroupsRecordsByStepAndOffset() {
        let records = [
            record(stepIndex: 0, packetIndex: 0, label: "baseline", changes: []),
            record(
                stepIndex: 1,
                packetIndex: 0,
                label: "switch 1",
                changes: [change(offset: 1, previous: 0x00, current: 0x7f)]
            ),
            record(
                stepIndex: 1,
                packetIndex: 1,
                label: "switch 1",
                changes: [
                    change(offset: 1, previous: 0x7f, current: 0x00),
                    change(offset: 2, previous: 0x10, current: 0x11),
                ]
            ),
        ]

        let summary = RigKontrolGuidedCaptureLogAnalyzer.summarize(records)

        XCTAssertEqual(summary.recordCount, 3)
        XCTAssertEqual(summary.stepSummaries.count, 2)
        XCTAssertEqual(summary.baselineChangingOffsets, [])
        XCTAssertEqual(summary.missingSteps.count, 10)
        XCTAssertEqual(summary.stepSummaries[0].stepLabel, "baseline")
        XCTAssertEqual(summary.stepSummaries[0].packetCount, 1)
        XCTAssertEqual(summary.stepSummaries[0].changedPacketCount, 0)
        XCTAssertEqual(summary.stepSummaries[0].offsets, [])

        let switchSummary = summary.stepSummaries[1]
        XCTAssertEqual(switchSummary.stepNumber, 2)
        XCTAssertEqual(switchSummary.stepLabel, "switch 1")
        XCTAssertEqual(switchSummary.packetCount, 2)
        XCTAssertEqual(switchSummary.changedPacketCount, 2)
        XCTAssertEqual(switchSummary.candidateOffsets, [1, 2])
        XCTAssertEqual(switchSummary.offsets.map(\.offset), [1, 2])
        XCTAssertEqual(switchSummary.offsets[0].changeCount, 2)
        XCTAssertEqual(
            switchSummary.offsets[0].transitions,
            [
                RigKontrolGuidedCaptureTransitionSummary(
                    previousValue: 0x00,
                    currentValue: 0x7f,
                    count: 1
                ),
                RigKontrolGuidedCaptureTransitionSummary(
                    previousValue: 0x7f,
                    currentValue: 0x00,
                    count: 1
                ),
            ]
        )
    }

    func testSummaryCountsRepeatedTransitions() {
        let records = [
            record(
                stepIndex: 1,
                packetIndex: 0,
                label: "switch 1",
                changes: [change(offset: 1, previous: 0x00, current: 0x7f)]
            ),
            record(
                stepIndex: 1,
                packetIndex: 1,
                label: "switch 1",
                changes: [change(offset: 1, previous: 0x00, current: 0x7f)]
            ),
        ]

        let summary = RigKontrolGuidedCaptureLogAnalyzer.summarize(records)

        XCTAssertEqual(summary.stepSummaries[0].offsets[0].transitions[0].count, 2)
    }

    func testSummaryReportsMissingExpectedSteps() {
        let expectedSteps = [
            RigKontrolGuidedCaptureStep(label: "baseline", instruction: "Hold still."),
            RigKontrolGuidedCaptureStep(label: "switch 1", instruction: "Press switch 1."),
            RigKontrolGuidedCaptureStep(label: "switch 2", instruction: "Press switch 2."),
        ]
        let records = [
            record(stepIndex: 0, packetIndex: 0, label: "baseline", changes: []),
            record(stepIndex: 2, packetIndex: 0, label: "switch 2", changes: []),
        ]

        let summary = RigKontrolGuidedCaptureLogAnalyzer.summarize(
            records,
            expectedSteps: expectedSteps
        )

        XCTAssertEqual(
            summary.missingSteps,
            [
                RigKontrolGuidedCaptureMissingStepSummary(
                    stepIndex: 1,
                    stepCount: 3,
                    step: expectedSteps[1]
                )
            ]
        )
    }

    func testSummaryReportsCandidateOffsetsBeyondBaselineChanges() {
        let records = [
            record(
                stepIndex: 0,
                packetIndex: 0,
                label: "baseline",
                changes: [
                    change(offset: 6, previous: 0x52, current: 0x54),
                    change(offset: 10, previous: nil, current: 0xa2),
                ]
            ),
            record(
                stepIndex: 1,
                packetIndex: 0,
                label: "switch 1",
                changes: [
                    change(offset: 0, previous: 0x03, current: 0x04),
                    change(offset: 6, previous: 0x54, current: 0x53),
                    change(offset: 10, previous: nil, current: 0xa2),
                    change(offset: 12, previous: 0x00, current: 0x7f),
                ]
            ),
        ]

        let summary = RigKontrolGuidedCaptureLogAnalyzer.summarize(records)

        XCTAssertEqual(summary.baselineChangingOffsets, [6])
        XCTAssertEqual(summary.stepSummaries[0].candidateOffsets, [])
        XCTAssertEqual(summary.stepSummaries[1].candidateOffsets, [6, 12])
    }

    func testSummaryReportsSwitchReportMasks() {
        let records = [
            record(
                stepIndex: 1,
                packetIndex: 0,
                label: "switch 2",
                bytes: [0x04, 0x20, 0x00, 0x00, 0x00, 0x00, 0x53, 0x04],
                changes: []
            ),
            record(
                stepIndex: 1,
                packetIndex: 1,
                label: "switch 2",
                bytes: [0x04, 0x10, 0x00, 0x00, 0x00, 0x00, 0x53, 0x04],
                changes: []
            ),
            record(
                stepIndex: 1,
                packetIndex: 2,
                label: "switch 2",
                bytes: [0x03, 0x00, 0x00],
                changes: []
            ),
        ]

        let summary = RigKontrolGuidedCaptureLogAnalyzer.summarize(records)

        XCTAssertEqual(summary.stepSummaries[0].switchReportCount, 2)
        XCTAssertEqual(summary.stepSummaries[0].switchReportMasks, [0x10, 0x20])
    }

    func testSummaryReportsExpressionRawRange() {
        let records = [
            record(
                stepIndex: 1,
                packetIndex: 0,
                label: "expression sweep",
                bytes: [0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4c],
                changes: []
            ),
            record(
                stepIndex: 1,
                packetIndex: 1,
                label: "expression sweep",
                bytes: [0x03, 0x00, 0x00, 0x00, 0x00, 0x02, 0x74],
                changes: []
            ),
            record(
                stepIndex: 1,
                packetIndex: 2,
                label: "expression sweep",
                bytes: [0x04, 0x10, 0x00],
                changes: []
            ),
        ]

        let summary = RigKontrolGuidedCaptureLogAnalyzer.summarize(records)

        XCTAssertEqual(summary.stepSummaries[0].expressionReportCount, 2)
        XCTAssertEqual(summary.stepSummaries[0].expressionRawMinimum, 76)
        XCTAssertEqual(summary.stepSummaries[0].expressionRawMaximum, 628)
    }

    private func record(
        stepIndex: Int,
        packetIndex: Int,
        label: String,
        bytes: [UInt8] = [0x03, 0x00, 0x54],
        changes: [RigKontrolGuidedCaptureChangeRecord]
    ) -> RigKontrolGuidedCaptureRecord {
        RigKontrolGuidedCaptureRecord(
            stepIndex: stepIndex,
            stepCount: 12,
            stepLabel: label,
            stepInstruction: "Test instruction",
            packetIndex: packetIndex,
            packetNumber: packetIndex + 1,
            timestampSecondsSince1970: 100 + Double(packetIndex),
            relativeTime: Double(packetIndex) / 10,
            endpointAddress: 0x81,
            length: bytes.count,
            bytes: bytes,
            changes: changes
        )
    }

    private func change(offset: Int, previous: UInt8?, current: UInt8?)
        -> RigKontrolGuidedCaptureChangeRecord
    {
        RigKontrolGuidedCaptureChangeRecord(
            offset: offset,
            previousValue: previous,
            currentValue: current
        )
    }
}
