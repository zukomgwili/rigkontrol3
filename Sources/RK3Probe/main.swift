import Darwin
import Foundation
import RigKontrol3Core

private let options = ProbeOptions(arguments: Array(CommandLine.arguments.dropFirst()))

if let analyzeCapturePath = options.analyzeCapturePath {
    print("RigKontrol3 Capture Analysis")
    print("macOS: \(ProcessInfo.processInfo.operatingSystemVersionString)")
    print("arch: \(hostArchitecture())")
    runCaptureAnalysis(path: analyzeCapturePath)
    exit(EXIT_SUCCESS)
}

print("RigKontrol3 Probe")
print("macOS: \(ProcessInfo.processInfo.operatingSystemVersionString)")
print("arch: \(hostArchitecture())")
print("")

let usbDevices = USBDeviceProbe.rigKontrol3Devices()
print("USB matches: \(usbDevices.count)")
for device in usbDevices {
    print("- product: \(device.identity.productName ?? "unknown")")
    print("  manufacturer: \(device.identity.manufacturerName ?? "unknown")")
    print("  serial: \(device.identity.serialNumber ?? "unknown")")
    print("  vid: \(hex(device.identity.vendorID))")
    print("  pid: \(hex(device.identity.productID))")
    print(
        "  class/subclass/protocol: \(optional(device.deviceClass))/\(optional(device.deviceSubClass))/\(optional(device.deviceProtocol))"
    )
    print(
        "  configurations: \(optional(device.configurationCount)), current: \(optional(device.currentConfiguration))"
    )
    print("  usb speed: \(optional(device.usbSpeed)), link speed: \(optional(device.linkSpeed))")
    print("  location: \(optional(device.locationID)), path: \(device.registryPath ?? "unknown")")
}
print("")

do {
    let descriptorDevices = try LibUSBDescriptorProbe.rigKontrol3Descriptors()
    print("libusb descriptor matches: \(descriptorDevices.count)")
    for device in descriptorDevices {
        print("- product: \(device.identity.productName ?? "unknown")")
        print("  manufacturer: \(device.identity.manufacturerName ?? "unknown")")
        print("  serial: \(device.identity.serialNumber ?? "unknown")")
        print("  bus/address: \(device.busNumber)/\(device.deviceAddress)")
        print("  speed: \(device.speed)")
        print("  open status: \(device.openStatus)")
        print("  configurations: \(device.configurations.count)")
        for configuration in device.configurations {
            print(
                "    config \(configuration.value): attributes=\(configuration.attributes), maxPower=\(configuration.maxPowerMilliamps)mA"
            )
            for interface in configuration.interfaces {
                print(
                    "      interface \(interface.number) alt \(interface.alternateSetting): class/subclass/protocol=\(interface.interfaceClass)/\(interface.interfaceSubClass)/\(interface.interfaceProtocol)"
                )
                for endpoint in interface.endpoints {
                    print(
                        "        endpoint \(hex(endpoint.address)): \(endpoint.direction) \(endpoint.transferType), maxPacket=\(endpoint.maxPacketSize), interval=\(endpoint.interval)"
                    )
                }
            }
        }
    }
} catch {
    print("libusb descriptor probe failed: \(error)")
}
print("")

let hidDevices = HIDDeviceProbe.rigKontrol3Devices()
print("HID matches: \(hidDevices.count)")
for device in hidDevices {
    print("- product: \(device.identity.productName ?? "unknown")")
    print("  manufacturer: \(device.identity.manufacturerName ?? "unknown")")
    print("  serial: \(device.identity.serialNumber ?? "unknown")")
    print(
        "  usage page/usage: \(optional(device.primaryUsagePage))/\(optional(device.primaryUsage))")
}
print("")

let midiEndpoints = MIDIEndpointProbe.rigKontrol3Endpoints()
print("MIDI endpoint matches: \(midiEndpoints.count)")
for endpoint in midiEndpoints {
    print("- \(endpoint.direction.rawValue): \(endpoint.name ?? "unknown")")
    print("  manufacturer: \(endpoint.manufacturer ?? "unknown")")
    print("  model: \(endpoint.model ?? "unknown")")
    print("  uniqueID: \(endpoint.uniqueID.map(String.init) ?? "unknown")")
}

if options.allSwitchesCapture {
    runGuidedCapture(
        title: "All-switch verification capture",
        steps: RigKontrolGuidedCapturePlan.allSwitchVerificationSteps,
        stepSeconds: options.stepSeconds,
        captureLogPath: options.captureLogPath
    )
} else if options.remainingControlsCapture {
    runGuidedCapture(
        title: "Remaining-control capture",
        steps: RigKontrolGuidedCapturePlan.remainingControlVerificationSteps,
        stepSeconds: options.stepSeconds,
        captureLogPath: options.captureLogPath
    )
} else if options.switchTwoAndNineCapture {
    runGuidedCapture(
        title: "Switch 2/9 verification capture",
        steps: RigKontrolGuidedCapturePlan.switchTwoAndNineVerificationSteps,
        stepSeconds: options.stepSeconds,
        captureLogPath: options.captureLogPath
    )
} else if options.switchEightCapture {
    runGuidedCapture(
        title: "Switch 8 verification capture",
        steps: RigKontrolGuidedCapturePlan.switchEightVerificationSteps,
        stepSeconds: options.stepSeconds,
        captureLogPath: options.captureLogPath
    )
} else if options.expressionCapture {
    runGuidedCapture(
        title: "Expression verification capture",
        steps: RigKontrolGuidedCapturePlan.expressionVerificationSteps,
        stepSeconds: options.stepSeconds,
        captureLogPath: options.captureLogPath
    )
} else if options.guidedCapture {
    runGuidedCapture(
        title: "Guided capture",
        steps: RigKontrolGuidedCapturePlan.defaultSteps,
        stepSeconds: options.stepSeconds,
        captureLogPath: options.captureLogPath
    )
} else if options.readBulk {
    print("")
    print("Bulk read: endpoint 0x81 for \(options.seconds)s")
    do {
        let summary = try LibUSBBulkEventReader.readRigKontrol3BulkInput(duration: options.seconds)
        print(summaryOutput(summary))
    } catch {
        print("bulk read failed: \(error)")
    }
}

private struct ProbeOptions {
    let readBulk: Bool
    let guidedCapture: Bool
    let allSwitchesCapture: Bool
    let remainingControlsCapture: Bool
    let switchTwoAndNineCapture: Bool
    let switchEightCapture: Bool
    let expressionCapture: Bool
    let seconds: TimeInterval
    let stepSeconds: TimeInterval
    let captureLogPath: String?
    let analyzeCapturePath: String?

    init(arguments: [String]) {
        readBulk = arguments.contains("--read-bulk")
        guidedCapture = arguments.contains("--guided-capture")
        allSwitchesCapture = arguments.contains("--all-switches-capture")
        remainingControlsCapture = arguments.contains("--remaining-controls-capture")
        switchTwoAndNineCapture = arguments.contains("--switch-2-9-capture")
        switchEightCapture = arguments.contains("--switch-8-capture")
        expressionCapture = arguments.contains("--expression-capture")
        captureLogPath = Self.value(after: "--capture-log", in: arguments)
        analyzeCapturePath = Self.value(after: "--analyze-capture", in: arguments)

        if let secondsIndex = arguments.firstIndex(of: "--seconds"),
            arguments.indices.contains(secondsIndex + 1),
            let parsedSeconds = TimeInterval(arguments[secondsIndex + 1])
        {
            seconds = max(0.1, parsedSeconds)
        } else {
            seconds = 5
        }

        if let stepSecondsIndex = arguments.firstIndex(of: "--step-seconds"),
            arguments.indices.contains(stepSecondsIndex + 1),
            let parsedStepSeconds = TimeInterval(arguments[stepSecondsIndex + 1])
        {
            stepSeconds = max(0.5, parsedStepSeconds)
        } else {
            stepSeconds = 2
        }
    }

    private static func value(after option: String, in arguments: [String]) -> String? {
        guard let optionIndex = arguments.firstIndex(of: option),
            arguments.indices.contains(optionIndex + 1)
        else {
            return nil
        }
        return arguments[optionIndex + 1]
    }
}

private func runCaptureAnalysis(path: String) {
    do {
        let records = try loadCaptureRecords(path: path)
        let summary = RigKontrolGuidedCaptureLogAnalyzer.summarize(
            records,
            expectedSteps: expectedCaptureSteps(for: records)
        )
        print("")
        print("Capture log analysis: \(captureFileURL(path: path).path)")
        print(captureAnalysisOutput(summary))
        print(captureDecodedEventOutput(records))
    } catch {
        print("capture analysis failed: \(error)")
    }
}

private func expectedCaptureSteps(for records: [RigKontrolGuidedCaptureRecord])
    -> [RigKontrolGuidedCaptureStep]
{
    guard let stepCount = records.map(\.stepCount).max() else {
        return RigKontrolGuidedCapturePlan.defaultSteps
    }

    if stepCount == RigKontrolGuidedCapturePlan.defaultSteps.count {
        return RigKontrolGuidedCapturePlan.defaultSteps
    }

    if stepCount == RigKontrolGuidedCapturePlan.remainingControlVerificationSteps.count {
        return RigKontrolGuidedCapturePlan.remainingControlVerificationSteps
    }

    if stepCount == RigKontrolGuidedCapturePlan.allSwitchVerificationSteps.count {
        return RigKontrolGuidedCapturePlan.allSwitchVerificationSteps
    }

    if stepCount == RigKontrolGuidedCapturePlan.switchTwoAndNineVerificationSteps.count {
        return RigKontrolGuidedCapturePlan.switchTwoAndNineVerificationSteps
    }

    if stepCount == RigKontrolGuidedCapturePlan.switchEightVerificationSteps.count {
        return RigKontrolGuidedCapturePlan.switchEightVerificationSteps
    }

    if stepCount == RigKontrolGuidedCapturePlan.expressionVerificationSteps.count {
        return RigKontrolGuidedCapturePlan.expressionVerificationSteps
    }

    let observedSteps = records.reduce(into: [Int: RigKontrolGuidedCaptureStep]()) {
        partialResult, record in
        partialResult[record.stepIndex] = RigKontrolGuidedCaptureStep(
            label: record.stepLabel,
            instruction: record.stepInstruction
        )
    }

    return (0..<stepCount).map { stepIndex in
        observedSteps[stepIndex]
            ?? RigKontrolGuidedCaptureStep(
                label: "step \(stepIndex + 1)",
                instruction: ""
            )
    }
}

private func loadCaptureRecords(path: String) throws -> [RigKontrolGuidedCaptureRecord] {
    let data = try Data(contentsOf: captureFileURL(path: path))
    let contents = String(decoding: data, as: UTF8.self)
    let decoder = JSONDecoder()

    return try contents.split(whereSeparator: \.isNewline).map { line in
        try decoder.decode(
            RigKontrolGuidedCaptureRecord.self,
            from: Data(line.utf8)
        )
    }
}

private func captureAnalysisOutput(_ summary: RigKontrolGuidedCaptureLogSummary) -> String {
    var lines: [String] = [
        "records: \(summary.recordCount)",
        "steps with packets: \(summary.stepSummaries.count)",
        "missing expected steps: \(summary.missingSteps.count)",
        "baseline-changing offsets: \(offsetList(summary.baselineChangingOffsets))",
    ]

    for missingStep in summary.missingSteps {
        lines.append(
            "! Missing Step \(missingStep.stepNumber)/\(missingStep.stepCount): \(missingStep.stepLabel)"
        )
    }

    for stepSummary in summary.stepSummaries {
        lines.append(
            "- Step \(stepSummary.stepNumber)/\(stepSummary.stepCount): \(stepSummary.stepLabel) packets=\(stepSummary.packetCount) changedPackets=\(stepSummary.changedPacketCount)"
        )
        if stepSummary.stepIndex != 0 {
            lines.append(
                "  candidate offsets: \(offsetList(stepSummary.candidateOffsets))"
            )
        }
        lines.append(
            "  switch reports: \(switchReportSummary(stepSummary))"
        )
        lines.append(
            "  expression reports: \(expressionReportSummary(stepSummary))"
        )
        if stepSummary.offsets.isEmpty {
            lines.append("  offsets: none")
        } else {
            for offsetSummary in stepSummary.offsets {
                lines.append(
                    "  offset \(offsetSummary.offset): changes=\(offsetSummary.changeCount) \(transitionSummary(offsetSummary.transitions))"
                )
            }
        }
    }

    return lines.joined(separator: "\n")
}

private func captureDecodedEventOutput(_ records: [RigKontrolGuidedCaptureRecord]) -> String {
    let maximumPrintedEvents = 80
    var parser = RigKontrolPacketParser()
    var totalEventCount = 0
    var eventLines: [String] = []

    for record in records {
        for event in parser.events(for: record.bytes) {
            totalEventCount += 1
            if eventLines.count < maximumPrintedEvents {
                eventLines.append(
                    "  Step \(record.stepNumber)/\(record.stepCount) \(record.stepLabel) packet \(record.packetNumber) +\(seconds(record.relativeTime))s: \(decodedEventSummary(event))"
                )
            }
        }
    }

    guard totalEventCount > 0 else {
        return "decoded events: none"
    }

    var lines = ["decoded events: \(totalEventCount)"]
    lines.append(contentsOf: eventLines)
    if totalEventCount > eventLines.count {
        lines.append("  ... \(totalEventCount - eventLines.count) more decoded events")
    }
    return lines.joined(separator: "\n")
}

private func decodedEventSummary(_ event: RigKontrolControlEvent) -> String {
    switch event {
    case .switchChanged(let index, let isPressed):
        "switch \(index) \(isPressed ? "pressed" : "released")"
    case .expressionChanged(let normalizedValue):
        "expression \(normalizedValue)"
    }
}

private func offsetList(_ offsets: [Int], none: String = "none") -> String {
    offsets.isEmpty ? none : offsets.map(String.init).joined(separator: ", ")
}

private func transitionSummary(_ transitions: [RigKontrolGuidedCaptureTransitionSummary])
    -> String
{
    transitions.map { transition in
        "\(transition.previousHex ?? "--") -> \(transition.currentHex ?? "--") x\(transition.count)"
    }.joined(separator: ", ")
}

private func switchReportSummary(_ stepSummary: RigKontrolGuidedCaptureStepSummary) -> String {
    guard stepSummary.switchReportCount > 0 else {
        return "none"
    }

    return
        "count=\(stepSummary.switchReportCount) masks=\(stepSummary.switchReportMasks.map { hex(Int($0)) }.joined(separator: ", "))"
}

private func expressionReportSummary(_ stepSummary: RigKontrolGuidedCaptureStepSummary) -> String {
    guard stepSummary.expressionReportCount > 0,
        let rawMinimum = stepSummary.expressionRawMinimum,
        let rawMaximum = stepSummary.expressionRawMaximum
    else {
        return "none"
    }

    return "count=\(stepSummary.expressionReportCount) raw=\(rawMinimum)...\(rawMaximum)"
}

private func captureFileURL(path: String) -> URL {
    if path.hasPrefix("/") {
        return URL(fileURLWithPath: path)
    }

    return URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
        .appendingPathComponent(path)
}

private func runGuidedCapture(
    title: String,
    steps: [RigKontrolGuidedCaptureStep],
    stepSeconds: TimeInterval,
    captureLogPath: String?
) {
    var previousBytes: [UInt8]?
    let captureLog = captureLogPath.flatMap { path in
        do {
            return try JSONLinesCaptureLog(path: path)
        } catch {
            print("capture log failed: \(error)")
            return nil
        }
    }

    print("")
    print("\(title): \(steps.count) steps, \(stepSeconds)s each")
    print("Each capture window starts as soon as its step label appears.")
    if let captureLog {
        print("Capture log: \(captureLog.path)")
    }

    for (index, step) in steps.enumerated() {
        print("")
        print("Step \(index + 1)/\(steps.count): \(step.label)")
        print("Action: \(step.instruction)")
        print("Bulk read: endpoint 0x81 for \(stepSeconds)s")

        do {
            let summary = try LibUSBBulkEventReader.readRigKontrol3BulkInput(
                duration: stepSeconds)
            let observations = LibUSBBulkPacketAnalyzer.observations(
                for: summary.packets,
                previousBytes: previousBytes
            )
            let records = observations.map { observation in
                RigKontrolGuidedCaptureRecord(
                    stepIndex: index,
                    stepCount: steps.count,
                    step: step,
                    observation: observation
                )
            }
            do {
                try captureLog?.append(records)
            } catch {
                print("capture log write failed: \(error)")
            }
            print(summaryOutput(summary, previousBytes: previousBytes))
            previousBytes = summary.packets.last?.bytes ?? previousBytes
        } catch {
            print("bulk read failed: \(error)")
        }
    }
}

private final class JSONLinesCaptureLog {
    let path: String

    private let handle: FileHandle
    private let encoder: JSONEncoder

    init(path: String) throws {
        let url = captureFileURL(path: path)
        try FileManager.default.createDirectory(
            at: url.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        if !FileManager.default.fileExists(atPath: url.path) {
            FileManager.default.createFile(atPath: url.path, contents: Data())
        }

        self.path = url.path
        handle = try FileHandle(forWritingTo: url)
        try handle.truncate(atOffset: 0)

        encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
    }

    deinit {
        try? handle.close()
    }

    func append(_ records: [RigKontrolGuidedCaptureRecord]) throws {
        for record in records {
            let data = try encoder.encode(record)
            try handle.write(contentsOf: data)
            try handle.write(contentsOf: Data([0x0A]))
        }
    }

}

private func summaryOutput(
    _ summary: LibUSBBulkReadSummary,
    previousBytes: [UInt8]? = nil
) -> String {
    var lines: [String] = [
        "open: \(summary.openStatus)",
        "set configuration: \(summary.configurationStatus ?? "not attempted")",
        "claim interface: \(summary.claimStatus ?? "not attempted")",
        "set alt: \(summary.alternateSettingStatus ?? "not attempted")",
        "release: \(summary.releaseStatus ?? "not attempted")",
    ]

    let observations = LibUSBBulkPacketAnalyzer.observations(
        for: summary.packets,
        previousBytes: previousBytes
    )
    lines.append("packets: \(observations.count)")
    for observation in observations {
        lines.append(
            "- \(packetNumber(observation.packetIndex)) +\(seconds(observation.relativeTime))s endpoint \(hex(observation.endpointAddress)) len=\(observation.length) \(hexBytes(observation.bytes))"
        )
        lines.append(
            "  changes: \(changeSummary(observation, hasPreviousBytes: previousBytes != nil))")
    }

    if !summary.nonTimeoutErrors.isEmpty {
        lines.append("errors: \(summary.nonTimeoutErrors.joined(separator: ", "))")
    }

    return lines.joined(separator: "\n")
}

private func hostArchitecture() -> String {
    #if arch(arm64)
        "arm64"
    #elseif arch(x86_64)
        "x86_64"
    #else
        "unknown"
    #endif
}

private func hex(_ value: Int) -> String {
    "0x" + String(value, radix: 16)
}

private func hexBytes(_ bytes: [UInt8]) -> String {
    bytes.map { byte in
        hexByte(byte)
    }.joined(separator: " ")
}

private func hexByte(_ byte: UInt8) -> String {
    let value = String(byte, radix: 16, uppercase: false)
    return value.count == 1 ? "0" + value : value
}

private func optionalHexByte(_ byte: UInt8?) -> String {
    byte.map(hexByte) ?? "--"
}

private func packetNumber(_ index: Int) -> String {
    String(format: "#%03d", index + 1)
}

private func seconds(_ interval: TimeInterval) -> String {
    String(format: "%.3f", interval)
}

private func changeSummary(
    _ observation: LibUSBBulkPacketObservation,
    hasPreviousBytes: Bool = false
) -> String {
    guard !observation.changedBytes.isEmpty else {
        if observation.packetIndex == 0, !hasPreviousBytes {
            return "baseline"
        }
        return "none"
    }

    return observation.changedBytes.map { change in
        "offset \(change.offset): \(optionalHexByte(change.previousValue)) -> \(optionalHexByte(change.currentValue))"
    }.joined(separator: ", ")
}

private func optional(_ value: Int?) -> String {
    value.map(String.init) ?? "unknown"
}
