import Foundation
@testable import RigKontrol3Core
import XCTest

final class CoreAudioUSBEventReaderTests: XCTestCase {
    func testInputKeepsDrainingDuringStartupAndCleanupWrites() throws {
        let acquiredBeforeStartup = Date().addingTimeInterval(-1)
        let connection = TestCoreAudioUSBConnection(
            initialPacket: RigKontrolUSBReadPacket(
                endpointAddress: 0x81, timestamp: acquiredBeforeStartup, bytes: [0]),
            backpressuredWrites: true)
        var packets: [RigKontrolUSBReadPacket] = []
        var results: [RigKontrolUSBWriteResult] = []

        let summary = try CoreAudioUSBEventReader.stream(
            connectionFactory: { connection },
            didOpen: { writer in
                // Each command queues input which must be consumed before the
                // write completes. One priming read cannot satisfy this device.
                for command: [UInt8] in [[0x05], [0x0b, 1, 10, 0], [0x04]] {
                    results.append(writer.write(command))
                }
            },
            willClose: { results.append($0.write([0x05])) },
            shouldContinue: { packets.count < 4 },
            packetHandler: { packet, _ in packets.append(packet) })

        XCTAssertEqual(results.map(\.status), Array(repeating: "IOKIT_SUCCESS", count: 4))
        XCTAssertEqual(results.map(\.transferred), [1, 4, 1, 1])
        XCTAssertEqual(packets.map(\.bytes), [[0], [1], [2], [3]])
        XCTAssertEqual(packets.first?.timestamp, acquiredBeforeStartup)
        XCTAssertEqual(summary.packetCount, 4)
        XCTAssertTrue(summary.nonTimeoutErrors.isEmpty)
        let state = connection.snapshot()
        XCTAssertEqual(state.commands, [[0x05], [0x0b, 1, 10, 0], [0x04], [0x05]])
        XCTAssertEqual(state.consumedPackets, 5, "Cleanup also needs an active input reader")
        XCTAssertEqual(state.closeCount, 1)
        XCTAssertEqual(state.activeReadsAtClose, 0)
        XCTAssertEqual(state.operationsAfterClose, 0)
    }

    func testInitialReadFailureClosesWithoutRunningStartupOrCleanupCommands() throws {
        let connection = TestCoreAudioUSBConnection()
        connection.disconnect()
        var callbacks: [String] = []

        let summary = try CoreAudioUSBEventReader.stream(
            connectionFactory: { connection },
            didOpen: { _ in callbacks.append("open") },
            willClose: { _ in callbacks.append("close") },
            shouldContinue: { true },
            packetHandler: { _, _ in XCTFail("A failed initial read must not deliver a packet") })

        XCTAssertEqual(summary.nonTimeoutErrors, ["IOKIT_NO_DEVICE"])
        XCTAssertTrue(callbacks.isEmpty)
        XCTAssertTrue(connection.snapshot().commands.isEmpty)
        XCTAssertEqual(connection.snapshot().closeCount, 1)
        XCTAssertEqual(connection.snapshot().activeReadsAtClose, 0)
    }

    func testStoppingDuringAnIdleReadJoinsBeforeClosingTheConnection() {
        let connection = TestCoreAudioUSBConnection()
        let control = USBStreamControl()
        let finished = DispatchSemaphore(value: 0)
        DispatchQueue.global().async {
            do {
                let summary = try CoreAudioUSBEventReader.stream(
                    connectionFactory: { connection },
                    didOpen: { _ in control.markOpened() },
                    shouldContinue: { control.shouldContinue },
                    packetHandler: { _, _ in XCTFail("An idle connection should deliver no packets") })
                control.finish(errors: summary.nonTimeoutErrors)
            } catch {
                control.finish(errors: [String(describing: error)])
            }
            finished.signal()
        }
        XCTAssertTrue(control.waitUntilOpened())
        XCTAssertTrue(connection.wait { $0.activeReads > 0 })

        let stoppedAt = ProcessInfo.processInfo.systemUptime
        control.stop()
        XCTAssertEqual(finished.wait(timeout: .now() + 1), .success)
        XCTAssertLessThan(ProcessInfo.processInfo.systemUptime - stoppedAt, 0.5)
        XCTAssertTrue(control.errors.isEmpty)
        let state = connection.snapshot()
        XCTAssertEqual(state.closeCount, 1)
        XCTAssertEqual(state.activeReadsAtClose, 0)
        XCTAssertEqual(state.operationsAfterClose, 0)
    }

    func testSessionRecoversThroughRealStreamAndKeepsMIDIPortsAcrossStopAndSleep() {
        let rig = CoreAudioUSBSessionRig()
        let session = rig.makeSession()
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        guard rig.wait(until: { $0.connections.count == 1 && $0.statuses.last == .connected }) else {
            XCTFail("The real stream did not complete its initial session handshake")
            return
        }
        XCTAssertTrue(rig.snapshot().statuses.contains(.waitingForDevice))
        let first = rig.snapshot().connections[0]
        first.enqueue([0x04, 0x10, 0, 0, 0, 0, 0x53, 0x04])
        XCTAssertTrue(rig.wait { $0.messages.count == 1 })

        first.disconnect()
        guard rig.wait(until: {
            $0.connections.count == 2 && $0.statuses.filter { $0 == .connected }.count == 2
        }) else {
            XCTFail("The session did not establish a fresh stream after disconnect")
            return
        }
        XCTAssertEqual(first.snapshot().closeCount, 1)
        XCTAssertEqual(rig.snapshot().statuses.filter { $0 == .waitingForDevice }.count, 2)
        XCTAssertEqual(rig.snapshot().streamErrors.first, ["IOKIT_NO_DEVICE"])
        let second = rig.snapshot().connections[1]
        // Reconnect reports the switch held. Its release is baseline recovery,
        // while the following deliberate press must reach MainStage once.
        second.enqueue([0x04, 0x10, 0, 0, 0, 0, 0x53, 0x04])
        second.enqueue([0x04, 0, 0, 0, 0, 0, 0x53, 0x04])
        second.enqueue([0x04, 0x10, 0, 0, 0, 0, 0x53, 0x04])
        XCTAssertTrue(rig.wait { $0.messages.count == 2 })
        XCTAssertEqual(rig.snapshot().messages.map(\.bytes), [[0xbf, 20, 127], [0xbf, 20, 127]])

        session.stop()
        XCTAssertTrue(rig.wait { $0.activeConnections == 0 && $0.statuses.last == .stopped })
        session.start()
        XCTAssertTrue(rig.wait { $0.connections.count == 3 && $0.statuses.last == .connected })
        session.suspend()
        XCTAssertTrue(rig.wait { $0.activeConnections == 0 && $0.statuses.last == .sleeping })
        session.resume()
        XCTAssertTrue(rig.wait { $0.connections.count == 4 && $0.statuses.last == .connected })
        session.shutdown()
        XCTAssertTrue(session.waitUntilShutdown(timeout: 2))

        let state = rig.snapshot()
        XCTAssertEqual(state.factoryAttempts, 5, "The first attempt has no device")
        XCTAssertEqual(state.maximumConnections, 1)
        XCTAssertEqual(state.sourcesCreated, 1)
        XCTAssertEqual(state.sourcesDisposed, 1)
        XCTAssertEqual(state.destinationsCreated, 1)
        XCTAssertEqual(state.destinationsDisposed, 1)
        XCTAssertEqual(state.messages.count, 2, "Recovery and cleanup must not send controller MIDI")
        for connection in state.connections {
            XCTAssertEqual(connection.snapshot().closeCount, 1)
            XCTAssertEqual(connection.snapshot().activeReadsAtClose, 0)
            XCTAssertEqual(connection.snapshot().operationsAfterClose, 0)
        }
    }

    func testSessionTranslatesNativeOpenBusyAndRemovalBeforeRetrying() {
        let rig = CoreAudioUSBSessionRig(factoryErrors: [
            .operationFailed("USBDeviceOpen: IOKIT_EXCLUSIVE_ACCESS"),
            .operationFailed("USBDeviceOpen: IOKIT_NO_DEVICE"),
        ])
        let session = rig.makeSession()
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        guard rig.wait(until: { $0.statuses.last == .connected }) else {
            XCTFail("The session did not retry native open failures through the real stream")
            return
        }

        let state = rig.snapshot()
        XCTAssertEqual(state.factoryAttempts, 3)
        XCTAssertTrue(state.statuses.contains {
            guard case .failed(let detail) = $0 else { return false }
            return detail.contains("IOKIT_EXCLUSIVE_ACCESS") && detail.contains("Stop RK3 audio")
        }, "A busy native open must explain how the user can release the device")
        XCTAssertTrue(state.statuses.contains(.waitingForDevice),
            "Unplugging during native open must be treated as device absence")
        XCTAssertEqual(state.sourcesCreated, 1)
        XCTAssertEqual(state.destinationsCreated, 1)
    }
}

/// A bounded device model: it can disappear, reply to a switch query, and
/// require IN progress while an OUT command is pending. It owns no USB state.
private final class TestCoreAudioUSBConnection: CoreAudioUSBConnection, @unchecked Sendable {
    struct State {
        var commands: [[UInt8]] = []
        var activeReads = 0
        var consumedPackets = 0
        var closeCount = 0
        var activeReadsAtClose = -1
        var operationsAfterClose = 0
    }
    private let condition = NSCondition()
    private var state = State()
    private var packets: [RigKontrolUSBReadPacket]
    private var disconnected = false
    private var closed = false
    private let backpressuredWrites: Bool
    private let snapshotMask: UInt8
    private var nextSequence: UInt8 = 1
    private let onClose: () -> Void

    init(
        initialPacket: RigKontrolUSBReadPacket? = nil,
        backpressuredWrites: Bool = false,
        snapshotMask: UInt8 = 0,
        onClose: @escaping () -> Void = {}
    ) {
        packets = initialPacket.map { [$0] } ?? []
        self.backpressuredWrites = backpressuredWrites
        self.snapshotMask = snapshotMask
        self.onClose = onClose
    }

    func readPacket() -> RigKontrolUSBInputReadResult {
        condition.lock()
        state.activeReads += 1
        condition.broadcast()
        defer {
            state.activeReads -= 1
            condition.broadcast()
            condition.unlock()
        }
        if closed {
            state.operationsAfterClose += 1
            return .failed("Read after close")
        }
        if packets.isEmpty, !disconnected {
            _ = condition.wait(until: Date().addingTimeInterval(0.02))
        }
        if disconnected { return .failed("IOKIT_NO_DEVICE") }
        guard !packets.isEmpty else { return .idle }
        state.consumedPackets += 1
        condition.broadcast()
        return .packet(packets.removeFirst())
    }

    func write(_ bytes: [UInt8], timeoutMilliseconds: UInt32) -> RigKontrolUSBWriteResult {
        condition.lock()
        defer { condition.unlock() }
        if closed {
            state.operationsAfterClose += 1
            return RigKontrolUSBWriteResult(status: "Write after close", transferred: 0)
        }
        state.commands.append(bytes)
        if disconnected { return RigKontrolUSBWriteResult(status: "IOKIT_NO_DEVICE", transferred: 0) }
        if backpressuredWrites {
            let expectedConsumption = state.consumedPackets + 1
            packets.append(RigKontrolUSBReadPacket(
                endpointAddress: 0x81, timestamp: Date(), bytes: [nextSequence]))
            nextSequence &+= 1
            condition.broadcast()
            let deadline = Date().addingTimeInterval(Double(timeoutMilliseconds) / 1000)
            while state.consumedPackets < expectedConsumption {
                if !condition.wait(until: deadline) {
                    return RigKontrolUSBWriteResult(status: "Input backpressure timeout", transferred: 0)
                }
            }
        } else if bytes == [0x04] {
            packets.append(RigKontrolUSBReadPacket(
                endpointAddress: 0x81, timestamp: Date(), bytes: [0x04, snapshotMask, 0, 0, 0, 0, 0, 0]))
            condition.broadcast()
        }
        return RigKontrolUSBWriteResult(status: "IOKIT_SUCCESS", transferred: bytes.count)
    }

    func close() {
        condition.lock()
        state.closeCount += 1
        state.activeReadsAtClose = state.activeReads
        closed = true
        condition.broadcast()
        condition.unlock()
        onClose()
    }

    func enqueue(_ bytes: [UInt8]) {
        condition.lock()
        packets.append(RigKontrolUSBReadPacket(endpointAddress: 0x81, timestamp: Date(), bytes: bytes))
        condition.broadcast()
        condition.unlock()
    }

    func disconnect() {
        condition.lock()
        disconnected = true
        condition.broadcast()
        condition.unlock()
    }

    func snapshot() -> State {
        condition.lock()
        defer { condition.unlock() }
        return state
    }

    func wait(timeout: TimeInterval = 1, until predicate: (State) -> Bool) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        condition.lock()
        defer { condition.unlock() }
        while !predicate(state) {
            if !condition.wait(until: deadline) { return predicate(state) }
        }
        return true
    }
}

private final class USBStreamControl: @unchecked Sendable {
    private let condition = NSCondition()
    private var running = true
    private var opened = false
    private var failures: [String] = []
    var shouldContinue: Bool {
        condition.lock()
        defer { condition.unlock() }
        return running
    }
    var errors: [String] {
        condition.lock()
        defer { condition.unlock() }
        return failures
    }
    func markOpened() {
        condition.lock()
        opened = true
        condition.broadcast()
        condition.unlock()
    }
    func waitUntilOpened() -> Bool {
        let deadline = Date().addingTimeInterval(1)
        condition.lock()
        defer { condition.unlock() }
        while !opened {
            if !condition.wait(until: deadline) { return opened }
        }
        return true
    }
    func stop() {
        condition.lock()
        running = false
        condition.unlock()
    }
    func finish(errors: [String]) {
        condition.lock()
        failures = errors
        condition.unlock()
    }
}

private final class CoreAudioUSBSessionRig: @unchecked Sendable {
    struct State {
        var factoryAttempts = 0
        var connections: [TestCoreAudioUSBConnection] = []
        var activeConnections = 0
        var maximumConnections = 0
        var statuses: [RigKontrolControllerStatus] = []
        var streamErrors: [[String]] = []
        var messages: [MIDIMessage] = []
        var sourcesCreated = 0
        var sourcesDisposed = 0
        var destinationsCreated = 0
        var destinationsDisposed = 0
    }
    private let condition = NSCondition()
    private var state = State()
    private var factoryErrors: [RigKontrolUSBReadError]

    init(factoryErrors: [RigKontrolUSBReadError] = [.deviceNotFound]) {
        self.factoryErrors = factoryErrors
    }

    func makeSession() -> RigKontrolControllerSession {
        RigKontrolControllerSession(
            dependencies: RigKontrolControllerSessionDependencies(
                makeMIDISource: {
                    self.change { $0.sourcesCreated += 1 }
                    return TestMIDIOutput(rig: self)
                },
                makeFeedbackDestination: { _ in
                    self.change { $0.destinationsCreated += 1 }
                    return TestMIDIFeedback(rig: self)
                },
                stream: { didOpen, willClose, shouldContinue, packetHandler in
                    let summary = try CoreAudioUSBEventReader.stream(
                        connectionFactory: { try self.openConnection() },
                        didOpen: didOpen, willClose: willClose,
                        shouldContinue: shouldContinue, packetHandler: packetHandler)
                    self.change { $0.streamErrors.append(summary.nonTimeoutErrors) }
                    return summary
                }),
            retryInterval: 0.01,
            statusChanged: { status in self.change { $0.statuses.append(status) } },
            switchOutputMode: .pressOnly)
    }

    private func openConnection() throws -> TestCoreAudioUSBConnection {
        condition.lock()
        defer { condition.unlock() }
        state.factoryAttempts += 1
        if !factoryErrors.isEmpty {
            let error = factoryErrors.removeFirst()
            condition.broadcast()
            throw error
        }
        let connection = TestCoreAudioUSBConnection(
            snapshotMask: state.connections.count == 1 ? 0x10 : 0,
            onClose: { self.change { $0.activeConnections -= 1 } })
        state.connections.append(connection)
        state.activeConnections += 1
        state.maximumConnections = max(state.maximumConnections, state.activeConnections)
        condition.broadcast()
        return connection
    }

    func snapshot() -> State {
        condition.lock()
        defer { condition.unlock() }
        return state
    }

    func change(_ update: (inout State) -> Void) {
        condition.lock()
        update(&state)
        condition.broadcast()
        condition.unlock()
    }

    func wait(timeout: TimeInterval = 2, until predicate: (State) -> Bool) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        condition.lock()
        defer { condition.unlock() }
        while !predicate(state) {
            if !condition.wait(until: deadline) { return predicate(state) }
        }
        return true
    }

    private final class TestMIDIOutput: RigKontrolControllerMIDIOutput {
        let rig: CoreAudioUSBSessionRig
        init(rig: CoreAudioUSBSessionRig) { self.rig = rig }
        deinit { rig.change { $0.sourcesDisposed += 1 } }
        func send(_ message: MIDIMessage) throws { rig.change { $0.messages.append(message) } }
    }

    private final class TestMIDIFeedback: RigKontrolControllerFeedbackInput {
        let rig: CoreAudioUSBSessionRig
        init(rig: CoreAudioUSBSessionRig) { self.rig = rig }
        deinit { rig.change { $0.destinationsDisposed += 1 } }
    }
}
