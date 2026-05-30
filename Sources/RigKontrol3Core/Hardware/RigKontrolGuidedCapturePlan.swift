public struct RigKontrolGuidedCaptureStep: Equatable, Sendable {
    public let label: String
    public let instruction: String

    public init(label: String, instruction: String) {
        self.label = label
        self.instruction = instruction
    }
}

public enum RigKontrolGuidedCapturePlan {
    public static let baselineStep = RigKontrolGuidedCaptureStep(
        label: "baseline",
        instruction: "Leave all switches untouched and keep the expression pedal still."
    )

    public static let defaultSteps: [RigKontrolGuidedCaptureStep] = [
        baselineStep,
        RigKontrolGuidedCaptureStep(
            label: "switch 1",
            instruction: "Press and hold footswitch 1 until the next step label appears."
        ),
        RigKontrolGuidedCaptureStep(
            label: "switch 2",
            instruction: "Press and hold footswitch 2 until the next step label appears."
        ),
        RigKontrolGuidedCaptureStep(
            label: "switch 3",
            instruction: "Press and hold footswitch 3 until the next step label appears."
        ),
        RigKontrolGuidedCaptureStep(
            label: "switch 4",
            instruction: "Press and hold footswitch 4 until the next step label appears."
        ),
        RigKontrolGuidedCaptureStep(
            label: "switch 5",
            instruction: "Press and hold footswitch 5 until the next step label appears."
        ),
        RigKontrolGuidedCaptureStep(
            label: "switch 6",
            instruction: "Press and hold footswitch 6 until the next step label appears."
        ),
        RigKontrolGuidedCaptureStep(
            label: "switch 7",
            instruction: "Press and hold footswitch 7 until the next step label appears."
        ),
        RigKontrolGuidedCaptureStep(
            label: "switch 8",
            instruction: "Press and hold footswitch 8 until the next step label appears."
        ),
        RigKontrolGuidedCaptureStep(
            label: "switch 9",
            instruction: "Press and hold footswitch 9 until the next step label appears."
        ),
        RigKontrolGuidedCaptureStep(
            label: "toe switch",
            instruction:
                "Press and hold the pedal toe switch until the next step label appears, if present."
        ),
        RigKontrolGuidedCaptureStep(
            label: "expression sweep",
            instruction:
                "Sweep the expression pedal slowly from heel to toe and back for the whole window."
        ),
    ]

    public static let remainingControlVerificationSteps: [RigKontrolGuidedCaptureStep] = [
        baselineStep,
        RigKontrolGuidedCaptureStep(
            label: "switch 2",
            instruction: "Press and hold footswitch 2 until the next step label appears."
        ),
        RigKontrolGuidedCaptureStep(
            label: "switch 9",
            instruction: "Press and hold footswitch 9 until the next step label appears."
        ),
        RigKontrolGuidedCaptureStep(
            label: "toe switch",
            instruction:
                "Press and hold the pedal toe switch until the next step label appears, if present."
        ),
    ]

    public static let allSwitchVerificationSteps: [RigKontrolGuidedCaptureStep] = {
        var steps = [baselineStep]
        for switchIndex in 1...8 {
            steps.append(footswitchStep(index: switchIndex))
            steps.append(releaseStep(afterSwitch: switchIndex))
        }
        steps.append(
            RigKontrolGuidedCaptureStep(
                label: "switch 9 toe-down",
                instruction:
                    "Press and hold the expression pedal toe-down closed until the next step label appears. Do not sweep the pedal."
            )
        )
        steps.append(releaseStep(afterSwitch: 9))
        return steps
    }()

    public static let switchTwoAndNineVerificationSteps: [RigKontrolGuidedCaptureStep] = [
        baselineStep,
        RigKontrolGuidedCaptureStep(
            label: "switch 1 reference",
            instruction:
                "Press and hold footswitch 1 until the next step label appears. This reference checks the switch 2 mask conflict."
        ),
        RigKontrolGuidedCaptureStep(
            label: "release all 1",
            instruction: "Release all switches and keep the expression pedal still."
        ),
        RigKontrolGuidedCaptureStep(
            label: "switch 2 first pass",
            instruction: "Press and hold footswitch 2 until the next step label appears."
        ),
        RigKontrolGuidedCaptureStep(
            label: "release all 2",
            instruction: "Release all switches and keep the expression pedal still."
        ),
        RigKontrolGuidedCaptureStep(
            label: "switch 2 second pass",
            instruction: "Press and hold footswitch 2 again until the next step label appears."
        ),
        RigKontrolGuidedCaptureStep(
            label: "release all 3",
            instruction: "Release all switches and keep the expression pedal still."
        ),
        RigKontrolGuidedCaptureStep(
            label: "switch 9 first pass",
            instruction: "Press and hold footswitch 9 until the next step label appears."
        ),
        RigKontrolGuidedCaptureStep(
            label: "release all 4",
            instruction: "Release all switches and keep the expression pedal still."
        ),
        RigKontrolGuidedCaptureStep(
            label: "switch 9 second pass",
            instruction: "Press and hold footswitch 9 again until the next step label appears."
        ),
    ]

    public static let switchEightVerificationSteps: [RigKontrolGuidedCaptureStep] = [
        baselineStep,
        footswitchStep(index: 8),
        releaseStep(afterSwitch: 8),
    ]

    public static let expressionVerificationSteps: [RigKontrolGuidedCaptureStep] = [
        baselineStep,
        RigKontrolGuidedCaptureStep(
            label: "expression heel hold",
            instruction: "Move the expression pedal fully heel-back and hold it still."
        ),
        RigKontrolGuidedCaptureStep(
            label: "expression toe hold",
            instruction:
                "Move the expression pedal fully toe-down and hold it still. Do not press the toe switch harder than the end stop."
        ),
        RigKontrolGuidedCaptureStep(
            label: "expression heel to toe sweep",
            instruction:
                "Sweep the expression pedal slowly from full heel-back to full toe-down across the whole window."
        ),
        RigKontrolGuidedCaptureStep(
            label: "expression toe to heel sweep",
            instruction:
                "Sweep the expression pedal slowly from full toe-down back to full heel-back across the whole window."
        ),
    ]

    private static func footswitchStep(index: Int) -> RigKontrolGuidedCaptureStep {
        RigKontrolGuidedCaptureStep(
            label: "switch \(index)",
            instruction: "Press and hold footswitch \(index) until the next step label appears."
        )
    }

    private static func releaseStep(afterSwitch switchIndex: Int) -> RigKontrolGuidedCaptureStep {
        RigKontrolGuidedCaptureStep(
            label: "release after switch \(switchIndex)",
            instruction: "Release all switches and keep the expression pedal still."
        )
    }
}
