import Darwin
import Dispatch
import Foundation
import RigKontrol3Core

private let options = BridgeOptions(arguments: Array(CommandLine.arguments.dropFirst()))

if options.help {
    printUsage()
    exit(EXIT_SUCCESS)
}

if let invalidSwitchMode = options.invalidSwitchMode {
    print("Unsupported --switch-mode value: \(invalidSwitchMode)")
    printUsage()
    exit(EXIT_FAILURE)
}

if let invalidLEDMode = options.invalidLEDMode {
    print("Unsupported --leds value: \(invalidLEDMode)")
    printUsage()
    exit(EXIT_FAILURE)
}

if let invalidLEDTest = options.invalidLEDTest {
    print("Unsupported --led-test value: \(invalidLEDTest)")
    printUsage()
    exit(EXIT_FAILURE)
}

print("RigKontrol3 MIDI Bridge")
print("macOS: \(ProcessInfo.processInfo.operatingSystemVersionString)")
print("arch: \(hostArchitecture())")
print("")

do {
    let runState = BridgeRunState()
    let signalSources = installSignalHandlers(runState: runState)
    defer { signalSources.forEach { $0.cancel() } }

    if let ledTest = options.ledTest {
        try runLEDTest(ledTest, options: options, runState: runState)
        exit(EXIT_SUCCESS)
    }

    let midiSource = try CoreMIDIVirtualSource(name: options.sourceName)
    var pipeline = RigKontrolMIDIBridgePipeline(switchMode: options.switchMode)
    var ledState = RigKontrolLEDState()
    var outputCount = 0
    var midiSendErrorCount = 0
    var ledWriteErrorCount = 0

    print("CoreMIDI source: \(midiSource.name)")
    print("USB input: interface 0 alt 0 endpoint 0x81")
    print("MIDI mapping: channel 16, switches 1-8 to notes 1-8, expression to CC 11")
    print("Switch mode: \(options.switchMode.rawValue)")
    print("LED sync: \(options.ledMode.rawValue)")
    if let runSeconds = options.runSeconds {
        print("Stop mode: after \(seconds(runSeconds))s")
    } else {
        print("Stop mode: Ctrl-C")
    }
    print("")

    let startedAt = Date()
    let summary = try LibUSBBulkEventReader.streamRigKontrol3BulkInput(
        didOpen: { outputWriter in
            if options.ledMode == .follow {
                writeLEDState(
                    ledState,
                    outputWriter: outputWriter,
                    errorCount: &ledWriteErrorCount,
                    verbose: options.verbose,
                    reason: "initial"
                )
            }
        },
        willClose: { outputWriter in
            if options.ledMode == .follow {
                writeLEDState(
                    RigKontrolLEDState(),
                    outputWriter: outputWriter,
                    errorCount: &ledWriteErrorCount,
                    verbose: options.verbose,
                    reason: "shutdown"
                )
            }
        },
        shouldContinue: {
            let withinRunWindow =
                options.runSeconds.map {
                    Date().timeIntervalSince(startedAt) < $0
                } ?? true
            return runState.shouldContinue() && withinRunWindow
        }
    ) { packet, outputWriter in
        for output in pipeline.outputs(for: packet.bytes) {
            do {
                try midiSource.send(output.message)
                outputCount += 1
                if options.ledMode == .follow {
                    updateLEDState(
                        for: output.event,
                        ledState: &ledState,
                        outputWriter: outputWriter,
                        errorCount: &ledWriteErrorCount,
                        verbose: options.verbose
                    )
                }
                if options.verbose {
                    print("sent \(eventDescription(output.event)) -> \(midiBytes(output.message))")
                }
            } catch {
                midiSendErrorCount += 1
                print("MIDI send failed: \(error)")
            }
        }
    }

    print("")
    print("Stopped RigKontrol3 MIDI bridge.")
    print("open: \(summary.openStatus)")
    print("set configuration: \(summary.configurationStatus ?? "not attempted")")
    print("claim interface: \(summary.claimStatus ?? "not attempted")")
    print("set alt: \(summary.alternateSettingStatus ?? "not attempted")")
    print("release: \(summary.releaseStatus ?? "not attempted")")
    print("packets: \(summary.packetCount)")
    print("MIDI outputs sent: \(outputCount)")
    if midiSendErrorCount > 0 {
        print("MIDI send errors: \(midiSendErrorCount)")
    }
    if ledWriteErrorCount > 0 {
        print("LED write errors: \(ledWriteErrorCount)")
    }
    if !summary.nonTimeoutErrors.isEmpty {
        print("USB errors: \(summary.nonTimeoutErrors.joined(separator: ", "))")
        exit(EXIT_FAILURE)
    }
    if !isSuccessStatus(summary.openStatus)
        || !isSuccessStatus(summary.claimStatus)
        || !isSuccessStatus(summary.alternateSettingStatus)
    {
        exit(EXIT_FAILURE)
    }
    if midiSendErrorCount > 0 {
        exit(EXIT_FAILURE)
    }
    if ledWriteErrorCount > 0 {
        exit(EXIT_FAILURE)
    }
} catch {
    print("bridge failed: \(error)")
    exit(EXIT_FAILURE)
}

private enum BridgeLEDMode: String {
    case follow
    case off
}

private enum BridgeLEDTest: Equatable {
    case off
    case all
    case switchLED(Int)

    init?(rawValue: String) {
        if rawValue == "off" {
            self = .off
        } else if rawValue == "all" {
            self = .all
        } else if let switchIndex = Int(rawValue), (1...9).contains(switchIndex) {
            self = .switchLED(switchIndex)
        } else {
            return nil
        }
    }
}

private struct BridgeOptions {
    let sourceName: String
    let verbose: Bool
    let help: Bool
    let runSeconds: TimeInterval?
    let switchMode: RigKontrolSwitchMIDIMode
    let invalidSwitchMode: String?
    let ledMode: BridgeLEDMode
    let invalidLEDMode: String?
    let ledTest: BridgeLEDTest?
    let invalidLEDTest: String?

    init(arguments: [String]) {
        sourceName = Self.value(after: "--source-name", in: arguments) ?? "RigKontrol3 Bridge"
        verbose = arguments.contains("--verbose")
        help = arguments.contains("--help") || arguments.contains("-h")
        runSeconds = Self.value(after: "--seconds", in: arguments).flatMap(TimeInterval.init).map {
            max(0.1, $0)
        }

        if let switchModeValue = Self.value(after: "--switch-mode", in: arguments) {
            switchMode = RigKontrolSwitchMIDIMode(rawValue: switchModeValue) ?? .latch
            invalidSwitchMode =
                RigKontrolSwitchMIDIMode(rawValue: switchModeValue) == nil
                ? switchModeValue : nil
        } else {
            switchMode = .latch
            invalidSwitchMode = nil
        }

        if let ledModeValue = Self.value(after: "--leds", in: arguments) {
            ledMode = BridgeLEDMode(rawValue: ledModeValue) ?? .follow
            invalidLEDMode = BridgeLEDMode(rawValue: ledModeValue) == nil ? ledModeValue : nil
        } else {
            ledMode = .follow
            invalidLEDMode = nil
        }

        if let ledTestValue = Self.value(after: "--led-test", in: arguments) {
            ledTest = BridgeLEDTest(rawValue: ledTestValue)
            invalidLEDTest = ledTest == nil ? ledTestValue : nil
        } else {
            ledTest = nil
            invalidLEDTest = nil
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

private func runLEDTest(
    _ ledTest: BridgeLEDTest,
    options: BridgeOptions,
    runState: BridgeRunState
) throws {
    var ledState = RigKontrolLEDState()
    switch ledTest {
    case .off:
        break
    case .all:
        for switchIndex in 1...9 {
            ledState.setSwitchLED(index: switchIndex, isOn: true)
        }
    case .switchLED(let switchIndex):
        ledState.setSwitchLED(index: switchIndex, isOn: true)
    }

    var ledWriteErrorCount = 0
    let runSeconds = options.runSeconds ?? 3
    let startedAt = Date()

    print("LED test: \(ledTestDescription(ledTest))")
    print("USB output: interface 0 alt 0 endpoint 0x01")
    print("Stop mode: after \(seconds(runSeconds))s")
    print("")

    let summary = try LibUSBBulkEventReader.streamRigKontrol3BulkInput(
        didOpen: { outputWriter in
            writeLEDState(
                ledState,
                outputWriter: outputWriter,
                errorCount: &ledWriteErrorCount,
                verbose: true,
                reason: "test"
            )
        },
        willClose: { outputWriter in
            writeLEDState(
                RigKontrolLEDState(),
                outputWriter: outputWriter,
                errorCount: &ledWriteErrorCount,
                verbose: true,
                reason: "shutdown"
            )
        },
        shouldContinue: {
            runState.shouldContinue() && Date().timeIntervalSince(startedAt) < runSeconds
        }
    ) { _, _ in }

    print("")
    print("Stopped RigKontrol3 LED test.")
    print("open: \(summary.openStatus)")
    print("set configuration: \(summary.configurationStatus ?? "not attempted")")
    print("claim interface: \(summary.claimStatus ?? "not attempted")")
    print("set alt: \(summary.alternateSettingStatus ?? "not attempted")")
    print("release: \(summary.releaseStatus ?? "not attempted")")
    print("packets: \(summary.packetCount)")
    if ledWriteErrorCount > 0 {
        print("LED write errors: \(ledWriteErrorCount)")
    }
    if !summary.nonTimeoutErrors.isEmpty {
        print("USB errors: \(summary.nonTimeoutErrors.joined(separator: ", "))")
        exit(EXIT_FAILURE)
    }
    if !isSuccessStatus(summary.openStatus)
        || !isSuccessStatus(summary.claimStatus)
        || !isSuccessStatus(summary.alternateSettingStatus)
        || ledWriteErrorCount > 0
    {
        exit(EXIT_FAILURE)
    }
}

private final class BridgeRunState: @unchecked Sendable {
    private let lock = NSLock()
    private var running = true

    func shouldContinue() -> Bool {
        lock.lock()
        defer { lock.unlock() }
        return running
    }

    @discardableResult
    func requestStop() -> Bool {
        lock.lock()
        defer { lock.unlock() }

        let wasRunning = running
        running = false
        return wasRunning
    }
}

private func installSignalHandlers(runState: BridgeRunState) -> [DispatchSourceSignal] {
    signal(SIGINT, SIG_IGN)
    signal(SIGTERM, SIG_IGN)

    return [SIGINT, SIGTERM].map { signalNumber in
        let source = DispatchSource.makeSignalSource(signal: signalNumber)
        source.setEventHandler {
            if runState.requestStop() {
                print("\nStopping after the current USB read...")
            }
        }
        source.resume()
        return source
    }
}

private func printUsage() {
    print(
        """
        Usage: swift run rk3bridge [--source-name name] [--switch-mode latch|momentary|trigger] [--leds follow|off] [--led-test off|all|1...9] [--seconds value] [--verbose]

        Opens the attached Native Instruments Rig Kontrol 3 USB bulk endpoint,
        parses verified controller packets, and publishes MIDI through a CoreMIDI
        virtual source. The default switch mode is latch for Guitar Rig-style
        effect toggles. LEDs follow switch output by default. Press Ctrl-C to stop.
        """
    )
}

private func updateLEDState(
    for event: RigKontrolControlEvent,
    ledState: inout RigKontrolLEDState,
    outputWriter: LibUSBBulkOutputWriter,
    errorCount: inout Int,
    verbose: Bool
) {
    guard case .switchChanged(let index, let isPressed) = event, (1...9).contains(index) else {
        return
    }

    ledState.setSwitchLED(index: index, isOn: isPressed)
    writeLEDState(
        ledState,
        outputWriter: outputWriter,
        errorCount: &errorCount,
        verbose: verbose,
        reason: "switch \(index) \(isPressed ? "on" : "off")"
    )
}

private func writeLEDState(
    _ ledState: RigKontrolLEDState,
    outputWriter: LibUSBBulkOutputWriter,
    errorCount: inout Int,
    verbose: Bool,
    reason: String
) {
    let result = outputWriter.write(ledState.writeIOCommandBytes)
    guard isSuccessStatus(result.status), result.transferred == ledState.writeIOCommandBytes.count
    else {
        errorCount += 1
        print(
            "LED write failed (\(reason)): endpoint \(hex(outputWriter.endpointAddress)) status=\(result.status) transferred=\(result.transferred)"
        )
        return
    }

    if verbose {
        print("sent LEDs \(reason) -> \(ledBytes(ledState))")
    }
}

private func ledBytes(_ ledState: RigKontrolLEDState) -> String {
    let switchByte = ledState.payloadBytes[4]
    let pedalByte = ledState.payloadBytes[5]
    return "cmd=05 switches=\(hexByte(switchByte)) pedal=\(hexByte(pedalByte))"
}

private func ledTestDescription(_ ledTest: BridgeLEDTest) -> String {
    switch ledTest {
    case .off:
        "all switch LEDs off"
    case .all:
        "all switch LEDs on"
    case .switchLED(let switchIndex):
        "switch LED \(switchIndex) on"
    }
}

private func eventDescription(_ event: RigKontrolControlEvent) -> String {
    switch event {
    case .switchChanged(let index, let isPressed):
        "switch \(index) \(isPressed ? "pressed" : "released")"
    case .expressionChanged(let normalizedValue):
        "expression \(normalizedValue)"
    }
}

private func midiBytes(_ message: MIDIMessage) -> String {
    message.bytes.map { byte in
        let value = String(byte, radix: 16, uppercase: false)
        return value.count == 1 ? "0" + value : value
    }.joined(separator: " ")
}

private func hex(_ value: Int) -> String {
    "0x" + String(value, radix: 16)
}

private func hexByte(_ byte: UInt8) -> String {
    let value = String(byte, radix: 16, uppercase: false)
    return value.count == 1 ? "0" + value : value
}

private func isSuccessStatus(_ status: String?) -> Bool {
    status?.contains("LIBUSB_SUCCESS") == true
}

private func seconds(_ interval: TimeInterval) -> String {
    String(format: "%.1f", interval)
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
