import Foundation

public enum RigKontrolControllerStatus: Equatable, Sendable {
    case stopped
    case connecting
    case connected
    case waitingForDevice
    case sleeping
    case failed(String)
}

public enum RigKontrolLEDMode: Equatable, Sendable {
    case press
    case hostFeedback
}

public enum RigKontrolSwitchOutputMode: String, CaseIterable, Sendable {
    case momentary
    case pressOnly
}

/// Missing switch indices are unknown, which differs from a confirmed off state.
public struct RigKontrolLEDFeedbackState: Equatable, Sendable {
    public let knownSwitches: [Int: Bool]
    public var count: Int { knownSwitches.count }

    public init(knownSwitches: [Int: Bool] = [:]) {
        self.knownSwitches = knownSwitches.filter { (1...8).contains($0.key) }
    }
}

// These dependencies keep lifecycle tests independent of attached hardware and CoreMIDI.
protocol RigKontrolControllerMIDIOutput: AnyObject {
    func send(_ message: MIDIMessage) throws
}

extension CoreMIDIVirtualSource: RigKontrolControllerMIDIOutput {}

protocol RigKontrolControllerFeedbackInput: AnyObject {}
extension CoreMIDIVirtualFeedbackDestination: RigKontrolControllerFeedbackInput {}

struct RigKontrolControllerSessionDependencies {
    typealias Stream = (
        _ didOpen: @escaping (RigKontrolUSBOutputWriter) -> Void,
        _ willClose: @escaping (RigKontrolUSBOutputWriter) -> Void,
        _ shouldContinue: () -> Bool,
        _ packetHandler: (RigKontrolUSBReadPacket, RigKontrolUSBOutputWriter) -> Void
    ) throws -> RigKontrolUSBStreamSummary

    let makeMIDISource: () throws -> any RigKontrolControllerMIDIOutput
    let makeFeedbackDestination: (
        _ received: @escaping @Sendable (MIDIMessage) -> Void
    ) throws -> any RigKontrolControllerFeedbackInput
    let stream: Stream

    static var live: Self {
        Self(
            makeMIDISource: {
                try CoreMIDIVirtualSource(name: "RigKontrol3 Bridge", uniqueID: 0x524B3301)
            },
            makeFeedbackDestination: {
                try CoreMIDIVirtualFeedbackDestination(
                    name: "RigKontrol3 Bridge", uniqueID: 0x524B3302, received: $0)
            },
            stream: { didOpen, willClose, shouldContinue, packetHandler in
                try CoreAudioUSBEventReader.streamRigKontrol3BulkInput(
                    didOpen: didOpen, willClose: willClose,
                    shouldContinue: shouldContinue, packetHandler: packetHandler)
            }
        )
    }
}

/// Owns controller USB access on one worker. Callbacks run on that worker, never the main queue.
public final class RigKontrolControllerSession: @unchecked Sendable {
    private let condition = NSCondition()
    private var wantsRunning = false
    private var isSuspended = false
    private var shutdownRequested = false
    private var shutdownFinished = false
    private var epoch: UInt64 = 0
    private var expressionCalibration: ExpressionCalibration
    private var switchOutputMode: RigKontrolSwitchOutputMode
    private var pendingFeedbackMessages: [MIDIMessage] = []
    private var feedbackInvalidationRequested = false

    private let dependencies: RigKontrolControllerSessionDependencies
    private let retryInterval: TimeInterval
    private let statusChanged: @Sendable (RigKontrolControllerStatus) -> Void
    private let messageSent: @Sendable (MIDIMessage) -> Void
    private let rawExpressionChanged: @Sendable (Int) -> Void
    private let ledMode: RigKontrolLEDMode
    private let feedbackChanged: @Sendable (RigKontrolLEDFeedbackState) -> Void
    private let feedbackMessageReceived: @Sendable (MIDIMessage) -> Void
    // Only accessed by the worker.
    private var lastStatus: RigKontrolControllerStatus?
    private var lastSentSwitchValues: [Int: UInt8] = [:]
    private var hostFeedbackState = RigKontrolLEDFeedbackState()
    private var lastPublishedFeedbackState: RigKontrolLEDFeedbackState?

    public convenience init(
        statusChanged: @escaping @Sendable (RigKontrolControllerStatus) -> Void,
        messageSent: @escaping @Sendable (MIDIMessage) -> Void = { _ in },
        rawExpressionChanged: @escaping @Sendable (Int) -> Void = { _ in },
        expressionCalibration: ExpressionCalibration = ExpressionCalibration(),
        switchOutputMode: RigKontrolSwitchOutputMode = .momentary,
        ledMode: RigKontrolLEDMode = .press,
        feedbackChanged: @escaping @Sendable (RigKontrolLEDFeedbackState) -> Void = { _ in },
        feedbackMessageReceived: @escaping @Sendable (MIDIMessage) -> Void = { _ in }
    ) {
        self.init(
            dependencies: .live, retryInterval: 1,
            statusChanged: statusChanged, messageSent: messageSent,
            rawExpressionChanged: rawExpressionChanged, expressionCalibration: expressionCalibration,
            switchOutputMode: switchOutputMode, ledMode: ledMode,
            feedbackChanged: feedbackChanged, feedbackMessageReceived: feedbackMessageReceived)
    }

    init(
        dependencies: RigKontrolControllerSessionDependencies,
        retryInterval: TimeInterval = 1,
        statusChanged: @escaping @Sendable (RigKontrolControllerStatus) -> Void,
        messageSent: @escaping @Sendable (MIDIMessage) -> Void = { _ in },
        rawExpressionChanged: @escaping @Sendable (Int) -> Void = { _ in },
        expressionCalibration: ExpressionCalibration = ExpressionCalibration(),
        switchOutputMode: RigKontrolSwitchOutputMode = .momentary,
        ledMode: RigKontrolLEDMode = .press,
        feedbackChanged: @escaping @Sendable (RigKontrolLEDFeedbackState) -> Void = { _ in },
        feedbackMessageReceived: @escaping @Sendable (MIDIMessage) -> Void = { _ in }
    ) {
        self.dependencies = dependencies
        self.retryInterval = max(0.01, retryInterval)
        self.statusChanged = statusChanged
        self.messageSent = messageSent
        self.rawExpressionChanged = rawExpressionChanged
        self.expressionCalibration = expressionCalibration
        self.switchOutputMode = switchOutputMode
        self.ledMode = ledMode
        self.feedbackChanged = feedbackChanged
        self.feedbackMessageReceived = feedbackMessageReceived
        DispatchQueue(label: "com.zukomgwili.rigkontrol3.controller").async { [self] in
            runWorker()
        }
    }

    public func start() {
        changeControl {
            guard !wantsRunning else { return false }
            wantsRunning = true
            return true
        }
    }

    public func stop() {
        changeControl {
            guard wantsRunning else { return false }
            wantsRunning = false
            return true
        }
    }

    public func suspend() {
        changeControl {
            guard !isSuspended else { return false }
            isSuspended = true
            return true
        }
    }

    public func resume() {
        changeControl {
            guard isSuspended else { return false }
            isSuspended = false
            return true
        }
    }

    /// Updates the next expression report without interrupting USB or switch state.
    public func setExpressionCalibration(_ calibration: ExpressionCalibration) {
        condition.lock()
        if !shutdownRequested { expressionCalibration = calibration }
        condition.unlock()
    }

    /// A fresh USB snapshot prevents a held switch from crossing protocol modes.
    /// Changing the mode never emits controller MIDI by itself.
    public func setSwitchOutputMode(_ mode: RigKontrolSwitchOutputMode) {
        changeControl {
            guard switchOutputMode != mode else { return false }
            switchOutputMode = mode
            return true
        }
    }

    private func currentSwitchOutputMode() -> RigKontrolSwitchOutputMode {
        condition.lock()
        defer { condition.unlock() }
        return switchOutputMode
    }

    /// The host lifecycle owner calls this when the previous host state is no longer valid.
    /// It changes neither USB ownership nor controller MIDI output.
    public func invalidateHostFeedback() {
        condition.lock()
        if !shutdownRequested {
            pendingFeedbackMessages.removeAll()
            feedbackInvalidationRequested = true
            condition.broadcast()
        }
        condition.unlock()
    }

    private func enqueueFeedback(_ message: MIDIMessage) {
        condition.lock()
        if !shutdownRequested {
            // Overload must not leave stale states looking authoritative. Clear
            // uncertain history and retain new messages as a partial refresh.
            if pendingFeedbackMessages.count == 256 {
                pendingFeedbackMessages.removeAll()
                feedbackInvalidationRequested = true
            }
            pendingFeedbackMessages.append(message)
            condition.broadcast()
        }
        condition.unlock()
    }

    /// Only the worker consumes feedback and invokes public callbacks.
    private func drainFeedback() {
        condition.lock()
        let messages = pendingFeedbackMessages
        let invalidate = feedbackInvalidationRequested
        pendingFeedbackMessages.removeAll(keepingCapacity: true)
        feedbackInvalidationRequested = false
        condition.unlock()

        var known = invalidate ? [:] : hostFeedbackState.knownSwitches
        for message in messages {
            feedbackMessageReceived(message)
            if let feedback = RigKontrolLEDFeedback(message: message) {
                known[feedback.switchIndex] = feedback.isOn
            }
        }
        hostFeedbackState = RigKontrolLEDFeedbackState(knownSwitches: known)
        if lastPublishedFeedbackState != hostFeedbackState {
            lastPublishedFeedbackState = hostFeedbackState
            feedbackChanged(hostFeedbackState)
        }
    }

    private func currentCalibration() -> ExpressionCalibration {
        condition.lock()
        defer { condition.unlock() }
        return expressionCalibration
    }

    public func shutdown() {
        condition.lock()
        shutdownRequested = true
        epoch &+= 1
        condition.broadcast()
        condition.unlock()
    }

    /// Used by an off-main quit handler after `shutdown()`. Control methods never wait for USB.
    @discardableResult
    public func waitUntilShutdown(timeout: TimeInterval) -> Bool {
        let deadline = Date().addingTimeInterval(max(0, timeout))
        condition.lock()
        defer { condition.unlock() }
        while !shutdownFinished {
            if !condition.wait(until: deadline) { return shutdownFinished }
        }
        return true
    }

    private func changeControl(_ change: () -> Bool) {
        condition.lock()
        if !shutdownRequested, change() {
            epoch &+= 1
            condition.broadcast()
        }
        condition.unlock()
    }

    private func runWorker() {
        var midiSource: (any RigKontrolControllerMIDIOutput)?
        var feedbackDestination: (any RigKontrolControllerFeedbackInput)?
        var announcedEpoch: UInt64?
        while let runEpoch = waitForRunRequest() {
            if announcedEpoch != runEpoch {
                publish(.connecting)
                announcedEpoch = runEpoch
            }
            do {
                if midiSource == nil {
                    midiSource = try dependencies.makeMIDISource()
                    lastSentSwitchValues.removeAll()
                }
                if feedbackDestination == nil {
                    feedbackDestination = try dependencies.makeFeedbackDestination { [weak self] message in
                        self?.enqueueFeedback(message)
                    }
                }
                guard isActive(runEpoch), let midiSource else { continue }
                runUSBConnection(epoch: runEpoch, midiSource: midiSource)
            } catch {
                if isActive(runEpoch) { publish(.failed("MIDI ports: \(error)")) }
            }
            waitBeforeRetry(epoch: runEpoch)
        }
        // Dispose both endpoints before acknowledging shutdown to the caller.
        feedbackDestination = nil
        midiSource = nil
        publish(.stopped)
        condition.lock()
        shutdownFinished = true
        condition.broadcast()
        condition.unlock()
    }

    private func waitForRunRequest() -> UInt64? {
        while true {
            drainFeedback()
            condition.lock()
            if shutdownRequested {
                condition.unlock()
                return nil
            }
            if wantsRunning, !isSuspended {
                let currentEpoch = epoch
                condition.unlock()
                return currentEpoch
            }
            let idleEpoch = epoch
            let idleStatus: RigKontrolControllerStatus =
                wantsRunning && isSuspended ? .sleeping : .stopped
            condition.unlock()
            publish(idleStatus)
            condition.lock()
            if epoch == idleEpoch, !shutdownRequested,
                pendingFeedbackMessages.isEmpty, !feedbackInvalidationRequested
            { condition.wait() }
            condition.unlock()
        }
    }

    private func isActive(_ runEpoch: UInt64) -> Bool {
        condition.lock()
        defer { condition.unlock() }
        return !shutdownRequested && wantsRunning && !isSuspended && epoch == runEpoch
    }

    private func waitBeforeRetry(epoch runEpoch: UInt64) {
        let deadline = Date().addingTimeInterval(retryInterval)
        while true {
            drainFeedback()
            condition.lock()
            guard !shutdownRequested && wantsRunning && !isSuspended && epoch == runEpoch else {
                condition.unlock()
                return
            }
            if pendingFeedbackMessages.isEmpty, !feedbackInvalidationRequested {
                let signaled = condition.wait(until: deadline)
                condition.unlock()
                if !signaled { return }
            } else {
                condition.unlock()
            }
            if Date() >= deadline { return }
        }
    }

    private func runUSBConnection(epoch runEpoch: UInt64, midiSource: any RigKontrolControllerMIDIOutput) {
        var processor = RigKontrolControllerProcessor(expressionCalibration: currentCalibration())
        let outputMode = currentSwitchOutputMode()
        var failure: RigKontrolControllerStatus?
        var cleanupFailure: RigKontrolControllerStatus?
        var snapshotDeadline: TimeInterval?
        var queryIssuedAt: Date?
        var isReady = false
        var lastRawExpression: Int?
        var activeWriter: RigKontrolUSBOutputWriter?
        var lastWrittenLEDState: RigKontrolLEDState?

        func reconcileHostLEDs(using writer: RigKontrolUSBOutputWriter) {
            guard self.ledMode == .hostFeedback else { return }
            var target = RigKontrolLEDState()
            for (index, isOn) in self.hostFeedbackState.knownSwitches {
                target.setSwitchLED(index: index, isOn: isOn)
            }
            guard lastWrittenLEDState != target else { return }
            failure = self.writeLEDs(target, using: writer)
            if failure == nil { lastWrittenLEDState = target }
        }

        func snapshotTimedOut() -> Bool {
            guard !isReady, let snapshotDeadline,
                ProcessInfo.processInfo.systemUptime >= snapshotDeadline
            else { return false }
            failure = .failed("USB switch snapshot did not arrive within one second. Retrying automatically.")
            return true
        }

        do {
            let summary = try dependencies.stream(
                { writer in
                    guard self.isActive(runEpoch) else { return }
                    activeWriter = writer
                    failure = self.writeLEDs(RigKontrolLEDState(), using: writer)
                    guard failure == nil, self.isActive(runEpoch) else { return }
                    lastWrittenLEDState = RigKontrolLEDState()
                    // Linux caiaq input initialization enables digital reports and a
                    // 10 ms analog interval. Reapply it after every claim: a cold plug
                    // must not depend on settings retained from an earlier bridge.
                    failure = self.writeCommand(
                        [0x0b, 0x01, 0x0a, 0x00], operation: "enable automatic controls", using: writer)
                    guard failure == nil, self.isActive(runEpoch) else { return }
                    // Attached-unit verification on 2026-10-24: payload-free command 0x04
                    // returns the current switch mask with six zero suffix bytes. Ordinary
                    // switch transitions carry the expression/footer bytes instead. Wait for
                    // this explicit snapshot so queued reports cannot trigger an effect.
                    queryIssuedAt = Date()
                    failure = self.writeCommand([0x04], operation: "switch snapshot query", using: writer)
                    if failure == nil {
                        snapshotDeadline = ProcessInfo.processInfo.systemUptime + 1
                        self.publish(.connecting)
                    }
                },
                { writer in
                    cleanupFailure = self.writeLEDs(RigKontrolLEDState(), using: writer)
                },
                {
                    self.drainFeedback()
                    guard self.isActive(runEpoch), failure == nil, !snapshotTimedOut() else { return false }
                    if isReady, let activeWriter { reconcileHostLEDs(using: activeWriter) }
                    return failure == nil
                },
                { packet, writer in
                    guard self.isActive(runEpoch), failure == nil, !snapshotTimedOut() else { return }
                    if !isReady {
                        // Input buffered before the startup query is preserved;
                        // an older query reply cannot establish this attachment's baseline.
                        guard let queryIssuedAt, packet.timestamp >= queryIssuedAt,
                            Self.isSwitchSnapshot(packet.bytes)
                        else { return }
                        _ = processor.process(packet.bytes)
                        isReady = true
                        self.drainFeedback()
                        reconcileHostLEDs(using: writer)
                        if failure == nil, self.isActive(runEpoch) { self.publish(.connected) }
                        return
                    }
                    processor.setExpressionCalibration(self.currentCalibration())
                    if let raw = RigKontrolPacketParser.rawExpressionValue(from: packet.bytes),
                        raw != lastRawExpression
                    {
                        lastRawExpression = raw
                        self.rawExpressionChanged(raw)
                    }
                    let frame = processor.process(packet.bytes)
                    for message in frame.messages {
                        guard self.isActive(runEpoch) else { return }
                        if outputMode == .pressOnly, Self.switchController(in: message) != nil,
                            message.bytes[2] == 0
                        { continue }
                        do {
                            try self.send(message, through: midiSource, outputMode: outputMode)
                        } catch {
                            failure = .failed("MIDI send: \(error)")
                            return
                        }
                    }
                    if self.ledMode == .press, frame.ledsChanged, self.isActive(runEpoch) {
                        failure = self.writeLEDs(frame.ledState, using: writer)
                    }
                }
            )
            if isActive(runEpoch) {
                publish(failure ?? statusAfterStream(summary) ?? cleanupFailure ?? .waitingForDevice)
            }
        } catch RigKontrolUSBReadError.deviceNotFound {
            if isActive(runEpoch) { publish(.waitingForDevice) }
        } catch RigKontrolUSBReadError.operationFailed(let detail) {
            if isActive(runEpoch) { publish(Self.usbFailure("USB: \(detail)")) }
        } catch {
            if isActive(runEpoch) { publish(.failed("USB: \(error)")) }
        }
    }

    private func writeLEDs(
        _ state: RigKontrolLEDState, using writer: RigKontrolUSBOutputWriter
    ) -> RigKontrolControllerStatus? {
        writeCommand(state.writeIOCommandBytes, operation: "LED write", using: writer)
    }

    private static func switchController(in message: MIDIMessage) -> Int? {
        let bytes = message.bytes
        return bytes.count == 3 && bytes[0] == 0xbf && (20...27).contains(Int(bytes[1]))
            ? Int(bytes[1]) : nil
    }

    private func send(
        _ message: MIDIMessage, through source: any RigKontrolControllerMIDIOutput,
        outputMode: RigKontrolSwitchOutputMode
    ) throws {
        let bytes = message.bytes
        let switchController = Self.switchController(in: message)
        if outputMode == .momentary, let controller = switchController, bytes[2] == 127,
            lastSentSwitchValues[controller] == 127
        {
            // A release can be lost when USB disappears or the controller stops.
            // Re-arm a host's rising-edge detector only when a new physical press
            // arrives; reconnect, held baselines and shutdown emit no switch MIDI.
            let neutral = MIDIMessage.controlChange(
                channel: .rigKontrolDefault, controller: controller, value: 0)
            try source.send(neutral)
            lastSentSwitchValues[controller] = 0
            messageSent(neutral)
        }
        try source.send(message)
        if let controller = switchController { lastSentSwitchValues[controller] = bytes[2] }
        messageSent(message)
    }

    private func writeCommand(
        _ bytes: [UInt8], operation: String, using writer: RigKontrolUSBOutputWriter
    ) -> RigKontrolControllerStatus? {
        let result = writer.write(bytes)
        guard Self.isSuccess(result.status), result.transferred == bytes.count else {
            return Self.usbFailure(
                "\(operation): \(result.status), \(result.transferred)/\(bytes.count) bytes")
        }
        return nil
    }

    private static func isSwitchSnapshot(_ bytes: [UInt8]) -> Bool {
        bytes.count == 8 && bytes[0] == 0x04 && bytes[2...7].allSatisfy { $0 == 0 }
    }

    private func statusAfterStream(_ summary: RigKontrolUSBStreamSummary) -> RigKontrolControllerStatus? {
        for (operation, status) in [
            ("open", summary.openStatus), ("configuration", summary.configurationStatus),
            ("claim interface", summary.claimStatus),
            ("select controller interface", summary.alternateSettingStatus),
        ] {
            if let status, !Self.isSuccess(status) {
                return Self.usbFailure("USB \(operation): \(status)")
            }
        }
        if let error = summary.nonTimeoutErrors.first { return Self.usbFailure("USB read: \(error)") }
        if let status = summary.releaseStatus, !Self.isSuccess(status) {
            return Self.usbFailure("USB release: \(status)")
        }
        return nil
    }

    private static func isSuccess(_ status: String) -> Bool {
        status == "IOKIT_SUCCESS" || status == "LIBUSB_SUCCESS"
            || status == "LIBUSB_SUCCESS / LIBUSB_TRANSFER_COMPLETED"
    }

    private static func usbFailure(_ detail: String) -> RigKontrolControllerStatus {
        if detail.contains("LIBUSB_ERROR_NO_DEVICE") || detail.contains("IOKIT_NO_DEVICE") { return .waitingForDevice }
        if detail.contains("LIBUSB_ERROR_BUSY") || detail.contains("IOKIT_EXCLUSIVE_ACCESS")
            || detail.contains("IOKIT_BUSY") {
            return .failed("\(detail). Stop RK3 audio and other controller bridges; retrying automatically.")
        }
        return .failed("\(detail). Retrying automatically.")
    }

    private func publish(_ status: RigKontrolControllerStatus) {
        guard lastStatus != status else { return }
        lastStatus = status
        statusChanged(status)
    }
}
