import Foundation

public enum RigKontrolControllerStatus: Equatable, Sendable {
    case stopped
    case connecting
    case connected
    case waitingForDevice
    case sleeping
    case failed(String)
}

// These dependencies keep lifecycle tests independent of attached hardware and CoreMIDI.
protocol RigKontrolControllerMIDIOutput: AnyObject {
    func send(_ message: MIDIMessage) throws
}

extension CoreMIDIVirtualSource: RigKontrolControllerMIDIOutput {}

struct RigKontrolControllerSessionDependencies {
    typealias Stream = (
        _ didOpen: @escaping (LibUSBBulkOutputWriter) -> Void,
        _ willClose: @escaping (LibUSBBulkOutputWriter) -> Void,
        _ shouldContinue: () -> Bool,
        _ packetHandler: (LibUSBBulkReadPacket, LibUSBBulkOutputWriter) -> Void
    ) throws -> LibUSBBulkStreamSummary

    let makeMIDISource: () throws -> any RigKontrolControllerMIDIOutput
    let stream: Stream

    static var live: Self {
        Self(
            makeMIDISource: {
                try CoreMIDIVirtualSource(name: "RigKontrol3 Bridge", uniqueID: 0x524B3301)
            },
            stream: { didOpen, willClose, shouldContinue, packetHandler in
                try LibUSBBulkEventReader.streamRigKontrol3BulkInput(
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

    private let dependencies: RigKontrolControllerSessionDependencies
    private let retryInterval: TimeInterval
    private let statusChanged: @Sendable (RigKontrolControllerStatus) -> Void
    private let messageSent: @Sendable (MIDIMessage) -> Void
    private let rawExpressionChanged: @Sendable (Int) -> Void
    // Only accessed by the worker.
    private var lastStatus: RigKontrolControllerStatus?
    private var lastSentSwitchValues: [Int: UInt8] = [:]

    public convenience init(
        statusChanged: @escaping @Sendable (RigKontrolControllerStatus) -> Void,
        messageSent: @escaping @Sendable (MIDIMessage) -> Void = { _ in },
        rawExpressionChanged: @escaping @Sendable (Int) -> Void = { _ in },
        expressionCalibration: ExpressionCalibration = ExpressionCalibration()
    ) {
        self.init(
            dependencies: .live, retryInterval: 1,
            statusChanged: statusChanged, messageSent: messageSent,
            rawExpressionChanged: rawExpressionChanged, expressionCalibration: expressionCalibration)
    }

    init(
        dependencies: RigKontrolControllerSessionDependencies,
        retryInterval: TimeInterval = 1,
        statusChanged: @escaping @Sendable (RigKontrolControllerStatus) -> Void,
        messageSent: @escaping @Sendable (MIDIMessage) -> Void = { _ in },
        rawExpressionChanged: @escaping @Sendable (Int) -> Void = { _ in },
        expressionCalibration: ExpressionCalibration = ExpressionCalibration()
    ) {
        self.dependencies = dependencies
        self.retryInterval = max(0.01, retryInterval)
        self.statusChanged = statusChanged
        self.messageSent = messageSent
        self.rawExpressionChanged = rawExpressionChanged
        self.expressionCalibration = expressionCalibration
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
                guard isActive(runEpoch), let midiSource else { continue }
                runUSBConnection(epoch: runEpoch, midiSource: midiSource)
            } catch {
                if isActive(runEpoch) { publish(.failed("MIDI source: \(error)")) }
            }
            waitBeforeRetry(epoch: runEpoch)
        }
        // Dispose the endpoint before acknowledging shutdown to the caller.
        midiSource = nil
        publish(.stopped)
        condition.lock()
        shutdownFinished = true
        condition.broadcast()
        condition.unlock()
    }

    private func waitForRunRequest() -> UInt64? {
        while true {
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
            if epoch == idleEpoch, !shutdownRequested { condition.wait() }
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
        condition.lock()
        defer { condition.unlock() }
        while !shutdownRequested && wantsRunning && !isSuspended && epoch == runEpoch {
            if !condition.wait(until: deadline) { return }
        }
    }

    private func runUSBConnection(epoch runEpoch: UInt64, midiSource: any RigKontrolControllerMIDIOutput) {
        var processor = RigKontrolControllerProcessor(expressionCalibration: currentCalibration())
        var failure: RigKontrolControllerStatus?
        var cleanupFailure: RigKontrolControllerStatus?
        var snapshotDeadline: TimeInterval?
        var queryIssuedAt: Date?
        var isReady = false
        var lastRawExpression: Int?

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
                    failure = self.writeLEDs(RigKontrolLEDState(), using: writer)
                    guard failure == nil, self.isActive(runEpoch) else { return }
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
                { self.isActive(runEpoch) && failure == nil && !snapshotTimedOut() },
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
                        self.publish(.connected)
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
                        do {
                            try self.send(message, through: midiSource)
                        } catch {
                            failure = .failed("MIDI send: \(error)")
                            return
                        }
                    }
                    if frame.ledsChanged, self.isActive(runEpoch) {
                        failure = self.writeLEDs(frame.ledState, using: writer)
                    }
                }
            )
            if isActive(runEpoch) {
                publish(failure ?? statusAfterStream(summary) ?? cleanupFailure ?? .waitingForDevice)
            }
        } catch LibUSBBulkReadError.deviceNotFound {
            if isActive(runEpoch) { publish(.waitingForDevice) }
        } catch {
            if isActive(runEpoch) { publish(.failed("USB: \(error)")) }
        }
    }

    private func writeLEDs(
        _ state: RigKontrolLEDState, using writer: LibUSBBulkOutputWriter
    ) -> RigKontrolControllerStatus? {
        writeCommand(state.writeIOCommandBytes, operation: "LED write", using: writer)
    }

    private func send(_ message: MIDIMessage, through source: any RigKontrolControllerMIDIOutput) throws {
        let bytes = message.bytes
        let switchController: Int? =
            bytes.count == 3 && bytes[0] == 0xbf && (20...27).contains(Int(bytes[1]))
            ? Int(bytes[1]) : nil
        if let controller = switchController, bytes[2] == 127,
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
        _ bytes: [UInt8], operation: String, using writer: LibUSBBulkOutputWriter
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

    private func statusAfterStream(_ summary: LibUSBBulkStreamSummary) -> RigKontrolControllerStatus? {
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
        status == "LIBUSB_SUCCESS" || status == "LIBUSB_SUCCESS / LIBUSB_TRANSFER_COMPLETED"
    }

    private static func usbFailure(_ detail: String) -> RigKontrolControllerStatus {
        if detail.contains("LIBUSB_ERROR_NO_DEVICE") { return .waitingForDevice }
        if detail.contains("LIBUSB_ERROR_BUSY") {
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
