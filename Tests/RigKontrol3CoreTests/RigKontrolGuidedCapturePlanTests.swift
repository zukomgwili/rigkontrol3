import RigKontrol3Core
import XCTest

final class RigKontrolGuidedCapturePlanTests: XCTestCase {
    func testDefaultPlanStartsWithBaseline() {
        let firstStep = RigKontrolGuidedCapturePlan.defaultSteps.first

        XCTAssertEqual(firstStep?.label, "baseline")
        XCTAssertEqual(
            firstStep?.instruction,
            "Leave all switches untouched and keep the expression pedal still."
        )
    }

    func testDefaultPlanCoversNineFootswitches() {
        let switchLabels = RigKontrolGuidedCapturePlan.defaultSteps.map(\.label).filter {
            $0.hasPrefix("switch ")
        }

        XCTAssertEqual(
            switchLabels,
            [
                "switch 1",
                "switch 2",
                "switch 3",
                "switch 4",
                "switch 5",
                "switch 6",
                "switch 7",
                "switch 8",
                "switch 9",
            ]
        )
    }

    func testDefaultSwitchInstructionsAskUserToHoldControls() {
        let switchInstructions = RigKontrolGuidedCapturePlan.defaultSteps
            .filter { $0.label.hasPrefix("switch ") }
            .map(\.instruction)

        XCTAssertTrue(
            switchInstructions.allSatisfy { instruction in
                instruction.contains("Press and hold")
            })
    }

    func testDefaultPlanIncludesToeSwitchAndExpressionSweep() {
        let labels = RigKontrolGuidedCapturePlan.defaultSteps.map(\.label)

        XCTAssertTrue(labels.contains("toe switch"))
        XCTAssertEqual(labels.last, "expression sweep")
    }

    func testRemainingControlVerificationPlanFocusesUnverifiedControls() {
        XCTAssertEqual(
            RigKontrolGuidedCapturePlan.remainingControlVerificationSteps.map(\.label),
            ["baseline", "switch 2", "switch 9", "toe switch"]
        )
        XCTAssertTrue(
            RigKontrolGuidedCapturePlan.remainingControlVerificationSteps.dropFirst()
                .allSatisfy { step in
                    step.instruction.contains("Press and hold")
                }
        )
    }

    func testSwitchTwoAndNineVerificationPlanRepeatsTargetControls() {
        XCTAssertEqual(
            RigKontrolGuidedCapturePlan.switchTwoAndNineVerificationSteps.map(\.label),
            [
                "baseline",
                "switch 1 reference",
                "release all 1",
                "switch 2 first pass",
                "release all 2",
                "switch 2 second pass",
                "release all 3",
                "switch 9 first pass",
                "release all 4",
                "switch 9 second pass",
            ]
        )
    }

    func testAllSwitchVerificationPlanAddsReleaseWindows() {
        let labels = RigKontrolGuidedCapturePlan.allSwitchVerificationSteps.map(\.label)

        XCTAssertEqual(labels.first, "baseline")
        XCTAssertEqual(labels.count, 19)
        XCTAssertEqual(labels[1], "switch 1")
        XCTAssertEqual(labels[2], "release after switch 1")
        XCTAssertEqual(labels[17], "switch 9 toe-down")
        XCTAssertEqual(labels[18], "release after switch 9")
        XCTAssertEqual(labels.filter { $0.hasPrefix("release after switch") }.count, 9)
    }

    func testAllSwitchVerificationPlanDescribesSwitchNineAsToeDown() {
        let switchNineStep = RigKontrolGuidedCapturePlan.allSwitchVerificationSteps.first {
            $0.label == "switch 9 toe-down"
        }

        XCTAssertEqual(
            switchNineStep?.instruction,
            "Press and hold the expression pedal toe-down closed until the next step label appears. Do not sweep the pedal."
        )
    }

    func testSwitchEightVerificationPlanIsShortAndFocused() {
        XCTAssertEqual(
            RigKontrolGuidedCapturePlan.switchEightVerificationSteps.map(\.label),
            ["baseline", "switch 8", "release after switch 8"]
        )
    }

    func testExpressionVerificationPlanCapturesHoldsAndSweeps() {
        XCTAssertEqual(
            RigKontrolGuidedCapturePlan.expressionVerificationSteps.map(\.label),
            [
                "baseline",
                "expression heel hold",
                "expression toe hold",
                "expression heel to toe sweep",
                "expression toe to heel sweep",
            ]
        )
    }
}
