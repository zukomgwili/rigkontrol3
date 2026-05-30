import Darwin
import Dispatch
import Foundation
import RigKontrol3Core

private let options = BridgeOptions(arguments: Array(CommandLine.arguments.dropFirst()))

if options.help {
    printUsage()
    exit(EXIT_SUCCESS)
}

print("RigKontrol3 MIDI Bridge")
print("macOS: \(ProcessInfo.processInfo.operatingSystemVersionString)")
print("arch: \(hostArchitecture())")
print("")

do {
    let runState = BridgeRunState()
    let signalSources = installSignalHandlers(runState: runState)
    defer { signalSources.forEach { $0.cancel() } }

    let midiSource = try CoreMIDIVirtualSource(name: options.sourceName)
    var pipeline = RigKontrolMIDIBridgePipeline()
    var outputCount = 0
    var midiSendErrorCount = 0

    print("CoreMIDI source: \(midiSource.name)")
    print("USB input: interface 0 alt 0 endpoint 0x81")
    print("MIDI mapping: channel 16, switches 1-8 to notes 1-8, expression to CC 11")
    if let runSeconds = options.runSeconds {
        print("Stop mode: after \(seconds(runSeconds))s")
    } else {
        print("Stop mode: Ctrl-C")
    }
    print("")

    let startedAt = Date()
    let summary = try LibUSBBulkEventReader.streamRigKontrol3BulkInput(
        shouldContinue: {
            let withinRunWindow =
                options.runSeconds.map {
                    Date().timeIntervalSince(startedAt) < $0
                } ?? true
            return runState.shouldContinue() && withinRunWindow
        }
    ) { packet in
        for output in pipeline.outputs(for: packet.bytes) {
            do {
                try midiSource.send(output.message)
                outputCount += 1
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
} catch {
    print("bridge failed: \(error)")
    exit(EXIT_FAILURE)
}

private struct BridgeOptions {
    let sourceName: String
    let verbose: Bool
    let help: Bool
    let runSeconds: TimeInterval?

    init(arguments: [String]) {
        sourceName = Self.value(after: "--source-name", in: arguments) ?? "RigKontrol3 Bridge"
        verbose = arguments.contains("--verbose")
        help = arguments.contains("--help") || arguments.contains("-h")
        runSeconds = Self.value(after: "--seconds", in: arguments).flatMap(TimeInterval.init).map {
            max(0.1, $0)
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
        Usage: swift run rk3bridge [--source-name name] [--seconds value] [--verbose]

        Opens the attached Native Instruments Rig Kontrol 3 USB bulk endpoint,
        parses verified controller packets, and publishes MIDI through a CoreMIDI
        virtual source. Press Ctrl-C to stop.
        """
    )
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
