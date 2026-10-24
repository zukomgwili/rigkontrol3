import Foundation
@testable import RigKontrol3Core
import XCTest

final class RigKontrolControllerSessionTests: XCTestCase {
    func testOneMIDISourceSurvivesStopAndSleepUntilShutdown() {
        let rig = SessionRig()
        let session = rig.makeSession()
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }

        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        session.stop()
        XCTAssertTrue(rig.wait { $0.statuses.last == .stopped && $0.activeConnections == 0 })
        session.suspend()
        session.resume()
        XCTAssertEqual(rig.snapshot().attempts, 1)
        session.start()
        XCTAssertTrue(rig.wait { $0.attempts == 2 && $0.statuses.last == .connected })
        session.suspend()
        XCTAssertTrue(rig.wait { $0.statuses.last == .sleeping && $0.activeConnections == 0 })
        session.resume()
        XCTAssertTrue(rig.wait { $0.attempts == 3 && $0.statuses.last == .connected })
        XCTAssertEqual(rig.snapshot().sourcesCreated, 1)
        XCTAssertEqual(rig.snapshot().sourcesDisposed, 0)
        XCTAssertEqual(rig.snapshot().maximumConnections, 1)

        session.shutdown()
        XCTAssertTrue(session.waitUntilShutdown(timeout: 2))
        XCTAssertEqual(rig.snapshot().sourcesDisposed, 1)
        XCTAssertEqual(rig.snapshot().activeConnections, 0)
        XCTAssertTrue(rig.snapshot().sentMessages.isEmpty)
    }

    func testRapidStopStartClosesOldClaimBeforeOpeningAnother() {
        let rig = SessionRig()
        let session = rig.makeSession()
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })

        session.stop()
        session.start()

        XCTAssertTrue(rig.wait { $0.attempts == 2 && $0.statuses.last == .connected })
        XCTAssertEqual(rig.snapshot().maximumConnections, 1)
        XCTAssertEqual(rig.snapshot().sourcesCreated, 1)
        XCTAssertGreaterThanOrEqual(rig.snapshot().ledWrites, 3)
    }

    func testMissingDeviceAndBusyClaimRetryWithoutReportingConnectedPrematurely() {
        let rig = SessionRig(initialOutcomes: [.missing, .busy])
        let session = rig.makeSession(retryInterval: 0.01)
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()

        XCTAssertTrue(rig.wait { $0.attempts == 3 && $0.statuses.last == .connected })
        let states = rig.snapshot().statuses
        XCTAssertTrue(states.contains(.waitingForDevice))
        XCTAssertTrue(states.contains { state in
            guard case .failed(let detail) = state else { return false }
            return detail.contains("LIBUSB_ERROR_BUSY") && detail.contains("RK3 audio")
        })
        XCTAssertTrue(zip(states, states.dropFirst()).allSatisfy { $0 != $1 })
        XCTAssertEqual(states.filter { $0 == .connected }.count, 1)
        XCTAssertEqual(rig.snapshot().sourcesCreated, 1)
    }

    func testSuspendAndStopInterruptLongMissingDeviceRetry() {
        let rig = SessionRig(alwaysMissing: true)
        let session = rig.makeSession(retryInterval: 60)
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .waitingForDevice })

        session.suspend()
        XCTAssertTrue(rig.wait { $0.statuses.last == .sleeping })
        session.resume()
        XCTAssertTrue(rig.wait { $0.attempts == 2 && $0.statuses.last == .waitingForDevice })
        session.stop()
        XCTAssertTrue(rig.wait { $0.statuses.last == .stopped })
        XCTAssertEqual(rig.snapshot().sourcesCreated, 1)
    }

    func testHeldReconnectRearmsHostOnlyOnNextIntentionalPress() {
        let rig = SessionRig(snapshotMasks: [0, 0x10])
        let session = rig.makeSession(retryInterval: 0.01)
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        rig.enqueue([[0x04, 0x10, 0, 0, 0, 0, 0x53, 0x04]])
        XCTAssertTrue(rig.wait { $0.sentMessages.count == 1 })
        rig.disconnect()
        XCTAssertTrue(rig.wait { $0.attempts == 2 && $0.statuses.last == .connected })
        rig.enqueue([
            [0x04, 0x10, 0, 0, 0, 0, 0x53, 0x04],
            [0x04, 0, 0, 0, 0, 0, 0x53, 0x04],
        ])
        XCTAssertTrue(rig.wait { $0.processedPackets >= 5 })
        XCTAssertEqual(rig.snapshot().sentMessages.count, 1)
        rig.enqueue([[0x04, 0x10, 0, 0, 0, 0, 0x53, 0x04]])
        XCTAssertTrue(rig.wait { $0.sentMessages.count == 3 })
        let outputs = rig.snapshot().sentMessages
        XCTAssertEqual(outputs, [127, 0, 127].map {
            .controlChange(channel: MIDIChannel(16), controller: 20, value: $0)
        })
        // Model a host which toggles only on low-to-high CC transitions. Repeating
        // 127 after a lost release must not swallow the next deliberate gesture.
        var hostIsHigh = false
        var hostToggleCount = 0
        for message in outputs {
            let isHigh = message.bytes[2] > 0
            if isHigh, !hostIsHigh { hostToggleCount += 1 }
            hostIsHigh = isHigh
        }
        XCTAssertEqual(hostToggleCount, 2)
        XCTAssertEqual(rig.snapshot().reportedMessages, rig.snapshot().sentMessages)
        XCTAssertEqual(rig.snapshot().sourcesCreated, 1)
        session.stop()
        XCTAssertTrue(rig.wait { $0.statuses.last == .stopped })
        XCTAssertEqual(rig.snapshot().sentMessages.count, 3)
    }

    func testShortStartupLEDWriteFailsBeforeConnected() {
        let rig = SessionRig(shortLEDWrites: true)
        let session = rig.makeSession(retryInterval: 60)
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { state in
            guard case .failed(let detail) = state.statuses.last else { return false }
            return detail.contains("LED write") && detail.contains("63/64 bytes")
        })
        XCTAssertFalse(rig.snapshot().statuses.contains(.connected))
        XCTAssertTrue(rig.snapshot().sentMessages.isEmpty)
    }

    func testMIDISendFailureIsReportedWithoutSuccessfulMessageCallback() {
        let rig = SessionRig(failMIDISends: true)
        let session = rig.makeSession(retryInterval: 60)
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        rig.enqueue([[0x04, 0], [0x04, 0x10]])
        XCTAssertTrue(rig.wait { state in
            guard case .failed(let detail) = state.statuses.last else { return false }
            return detail.contains("MIDI send")
        })
        XCTAssertTrue(rig.snapshot().reportedMessages.isEmpty)
    }

    func testSnapshotDiscardsQueuedReportsAndRetainsFirstDeliberatePress() {
        let rig = SessionRig()
        rig.enqueue([
            [0x04, 0x10, 0, 0, 0, 0, 0x53, 0x04],
            [0x03, 0, 0, 0, 0, 0x02, 0x94],
        ])
        let session = rig.makeSession()
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        XCTAssertTrue(rig.snapshot().sentMessages.isEmpty)
        XCTAssertTrue(rig.snapshot().rawExpressions.isEmpty)
        XCTAssertEqual(rig.snapshot().queryWrites, 1)
        let commands = rig.snapshot().commands
        XCTAssertEqual(commands.prefix(3).compactMap(\.first), [0x05, 0x0b, 0x04])
        XCTAssertEqual(commands[1], [0x0b, 0x01, 0x0a, 0x00])
        XCTAssertEqual(commands[2], [0x04])

        rig.enqueue([[0x04, 0x10, 0, 0, 0, 0, 0x53, 0x04]])
        XCTAssertTrue(rig.wait { $0.sentMessages.count == 1 })
        XCTAssertEqual(
            rig.snapshot().sentMessages.first,
            .controlChange(channel: MIDIChannel(16), controller: 20, value: 127))
    }

    func testMissingSnapshotFailsWithoutPublishingAnyMIDIOrConnected() {
        let rig = SessionRig(automaticallyReplyToQuery: false)
        let session = rig.makeSession(retryInterval: 60)
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.queryWrites == 1 })
        rig.enqueue([
            [0x04, 0x10, 0, 0, 0, 0, 0x53, 0x04],
            [0x03, 0, 0, 0, 0, 0x02, 0x94],
        ])
        XCTAssertTrue(rig.wait { state in
            guard case .failed(let detail) = state.statuses.last else { return false }
            return detail.contains("switch snapshot did not arrive")
        })
        XCTAssertFalse(rig.snapshot().statuses.contains(.connected))
        XCTAssertTrue(rig.snapshot().sentMessages.isEmpty)
        XCTAssertTrue(rig.snapshot().rawExpressions.isEmpty)
    }

    func testBufferedSnapshotReadBeforeCurrentQueryCannotEstablishReadiness() {
        let rig = SessionRig(
            automaticallyReplyToQuery: false,
            primingPacket: [0x04, 0, 0, 0, 0, 0, 0, 0])
        let session = rig.makeSession()
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.processedPackets == 1 })
        XCTAssertEqual(rig.snapshot().statuses.last, .connecting)
        XCTAssertFalse(rig.snapshot().statuses.contains(.connected))
        XCTAssertTrue(rig.snapshot().sentMessages.isEmpty)

        // This reply was read after the current query and reports a held switch.
        rig.enqueue([[0x04, 0x10, 0, 0, 0, 0, 0, 0]])
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        XCTAssertTrue(rig.snapshot().sentMessages.isEmpty)
        rig.enqueue([[0x04, 0], [0x04, 0x10]])
        XCTAssertTrue(rig.wait { $0.processedPackets >= 4 })
        XCTAssertEqual(
            rig.snapshot().sentMessages,
            [.controlChange(channel: MIDIChannel(16), controller: 20, value: 127)])
    }

    func testShortSnapshotQueryWriteFailsBeforeConnected() {
        let rig = SessionRig(shortQueryWrites: true)
        let session = rig.makeSession(retryInterval: 60)
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { state in
            guard case .failed(let detail) = state.statuses.last else { return false }
            return detail.contains("switch snapshot query") && detail.contains("0/1 bytes")
        })
        XCTAssertFalse(rig.snapshot().statuses.contains(.connected))
        XCTAssertTrue(rig.snapshot().sentMessages.isEmpty)
    }

    func testShortAutomaticControlsWriteFailsBeforeQueryOrConnected() {
        let rig = SessionRig(shortAutomaticControlWrites: true)
        let session = rig.makeSession(retryInterval: 60)
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { state in
            guard case .failed(let detail) = state.statuses.last else { return false }
            return detail.contains("enable automatic controls") && detail.contains("3/4 bytes")
        })
        XCTAssertEqual(rig.snapshot().queryWrites, 0)
        XCTAssertFalse(rig.snapshot().statuses.contains(.connected))
        XCTAssertTrue(rig.snapshot().sentMessages.isEmpty)
    }

    func testHeldSnapshotAndReleaseDoNotSendSwitchMessage() {
        let rig = SessionRig(snapshotMasks: [0x10])
        let session = rig.makeSession()
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        rig.enqueue([
            [0x04, 0, 0, 0, 0, 0, 0x53, 0x04],
            [0x03, 0, 0, 0, 0, 0x02, 0x94],
        ])
        // Receiving expression proves the preceding release was processed too.
        XCTAssertTrue(rig.wait { $0.sentMessages.count == 1 })
        XCTAssertEqual(
            rig.snapshot().sentMessages,
            [.controlChange(channel: MIDIChannel(16), controller: 11, value: 127)])
        rig.enqueue([[0x04, 0x10, 0, 0, 0, 0, 0x53, 0x04]])
        XCTAssertTrue(rig.wait { $0.sentMessages.count == 2 })
        XCTAssertEqual(
            rig.snapshot().sentMessages.last,
            .controlChange(channel: MIDIChannel(16), controller: 20, value: 127))
    }

    func testSuccessfulNeutralRemainsRecordedWhenFollowingPressSendFails() {
        let rig = SessionRig(failMIDISendAttempts: [3], snapshotMasks: [0, 0x10, 0x10])
        let session = rig.makeSession()
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        rig.enqueue([[0x04, 0x10]])
        XCTAssertTrue(rig.wait { $0.sentMessages.count == 1 })
        rig.disconnect()
        XCTAssertTrue(rig.wait { $0.attempts == 2 && $0.statuses.last == .connected })
        rig.enqueue([[0x04, 0], [0x04, 0x10]])
        XCTAssertTrue(rig.wait { $0.attempts == 3 && $0.statuses.last == .connected })
        XCTAssertEqual(rig.snapshot().sentMessages.map { $0.bytes[2] }, [127, 0])
        rig.enqueue([[0x04, 0], [0x04, 0x10]])
        XCTAssertTrue(rig.wait { $0.processedPackets >= 8 })
        XCTAssertEqual(rig.snapshot().sentMessages.map { $0.bytes[2] }, [127, 0, 127])
        XCTAssertEqual(rig.snapshot().reportedMessages, rig.snapshot().sentMessages)
        XCTAssertEqual(rig.snapshot().sourcesCreated, 1)
    }

    func testCalibrationChangesInPlaceWithoutReplayingHeldSwitchOrReopeningUSB() {
        let rig = SessionRig()
        let session = rig.makeSession()
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        rig.enqueue([[0x04, 0x10], [0x03, 0, 0, 0, 0, 0, 100]])
        XCTAssertTrue(rig.wait { $0.sentMessages.count == 2 })

        session.setExpressionCalibration(ExpressionCalibration(rawMinimum: 100, rawMaximum: 600))
        rig.enqueue([
            [0x04, 0x10],
            [0x03, 0, 0, 0, 0, 0, 100],
            [0x03, 0, 0, 0, 0, 0x02, 0x58],
            [0x03, 0, 0, 0, 0, 0x02, 0x58],
            [0x03, 0, 0, 0, 0, 1],
            [0x7f, 0, 0, 0, 0, 0, 100],
            [0x04, 0],
        ])
        XCTAssertTrue(rig.wait { $0.processedPackets >= 10 })
        XCTAssertEqual(rig.snapshot().sentMessages, [
            .controlChange(channel: MIDIChannel(16), controller: 20, value: 127),
            .controlChange(channel: MIDIChannel(16), controller: 11, value: 7),
            .controlChange(channel: MIDIChannel(16), controller: 11, value: 0),
            .controlChange(channel: MIDIChannel(16), controller: 11, value: 127),
            .controlChange(channel: MIDIChannel(16), controller: 20, value: 0),
        ])
        XCTAssertEqual(rig.snapshot().rawExpressions, [100, 600])
        XCTAssertEqual(rig.snapshot().attempts, 1)
        XCTAssertEqual(rig.snapshot().sourcesCreated, 1)
        XCTAssertEqual(rig.snapshot().statuses.filter { $0 == .connected }.count, 1)
    }

    func testInitialCalibrationAndRawCallbackAreAppliedAgainAfterReconnect() {
        let rig = SessionRig()
        let session = rig.makeSession(
            expressionCalibration: ExpressionCalibration(rawMinimum: 100, rawMaximum: 600))
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        rig.enqueue([[0x03, 0, 0, 0, 0, 0, 100]])
        XCTAssertTrue(rig.wait { $0.sentMessages.count == 1 })
        rig.disconnect()
        XCTAssertTrue(rig.wait { $0.attempts == 2 && $0.statuses.last == .connected })
        rig.enqueue([[0x03, 0, 0, 0, 0, 0, 100]])
        XCTAssertTrue(rig.wait { $0.sentMessages.count == 2 })
        XCTAssertEqual(rig.snapshot().rawExpressions, [100, 100])
        XCTAssertEqual(rig.snapshot().sentMessages, [
            .controlChange(channel: MIDIChannel(16), controller: 11, value: 0),
            .controlChange(channel: MIDIChannel(16), controller: 11, value: 0),
        ])
        XCTAssertEqual(rig.snapshot().sourcesCreated, 1)
    }

    func testHostFeedbackChangesLEDsWhileInputIsIdleAndPhysicalPressesDoNotChangeThem() {
        let rig = SessionRig()
        let session = rig.makeSession(ledMode: .hostFeedback)
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        XCTAssertEqual(rig.snapshot().feedbackStates.last?.count, 0)
        rig.sendFeedback([
            .controlChange(channel: MIDIChannel(1), controller: 20, value: 127),
            .controlChange(channel: MIDIChannel(16), controller: 28, value: 127),
            .controlChange(channel: MIDIChannel(16), controller: 20, value: 64),
            .noteOn(channel: MIDIChannel(16), note: 20),
        ])
        XCTAssertTrue(rig.wait { $0.feedbackMessages.count == 4 })
        XCTAssertEqual(rig.snapshot().feedbackStates.last?.count, 0)
        XCTAssertEqual(rig.snapshot().ledWrites, 1)
        XCTAssertTrue(rig.snapshot().sentMessages.isEmpty)

        rig.sendFeedback([.controlChange(channel: MIDIChannel(16), controller: 20, value: 127)])
        XCTAssertTrue(rig.wait { $0.lastLEDState?.isSwitchLEDOn(index: 1) == true })
        XCTAssertEqual(rig.snapshot().processedPackets, 1)
        XCTAssertEqual(rig.snapshot().feedbackStates.last?.knownSwitches, [1: true])
        XCTAssertTrue(rig.snapshot().sentMessages.isEmpty)
        rig.enqueue([[0x04, 0x10], [0x04, 0], [0x04, 0x20], [0x04, 0]])
        XCTAssertTrue(rig.wait { $0.processedPackets >= 5 })
        XCTAssertEqual(rig.snapshot().sentMessages.count, 4)
        XCTAssertEqual(rig.snapshot().ledWrites, 2)
        XCTAssertTrue(rig.snapshot().lastLEDState?.isSwitchLEDOn(index: 1) == true)
        XCTAssertFalse(rig.snapshot().lastLEDState?.isSwitchLEDOn(index: 2) == true)

        rig.sendFeedback([.controlChange(channel: MIDIChannel(16), controller: 20, value: 0)])
        XCTAssertTrue(rig.wait { $0.lastLEDState?.isSwitchLEDOn(index: 1) == false })
        XCTAssertEqual(rig.snapshot().feedbackStates.last?.knownSwitches, [1: false])
        XCTAssertEqual(rig.snapshot().sentMessages.count, 4)
        XCTAssertFalse(rig.snapshot().ledWriteOnMainThread)
    }

    func testHostFeedbackInvalidationClearsUnknownWithoutResettingUSBOrSendingMIDI() {
        let rig = SessionRig()
        let session = rig.makeSession(ledMode: .hostFeedback)
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        rig.sendFeedback([
            .controlChange(channel: MIDIChannel(16), controller: 20, value: 127),
            .controlChange(channel: MIDIChannel(16), controller: 21, value: 0),
        ])
        XCTAssertTrue(rig.wait { $0.feedbackStates.last?.count == 2 && $0.lastLEDState?.isSwitchLEDOn(index: 1) == true })
        session.invalidateHostFeedback()
        XCTAssertTrue(rig.wait { $0.feedbackStates.last?.count == 0 && $0.lastLEDState == RigKontrolLEDState() })
        XCTAssertEqual(rig.snapshot().attempts, 1)
        XCTAssertEqual(rig.snapshot().sourcesCreated, 1)
        XCTAssertEqual(rig.snapshot().feedbackDestinationsCreated, 1)
        XCTAssertTrue(rig.snapshot().sentMessages.isEmpty)
    }

    func testHostFeedbackAndPortsSurviveStopSleepAndUSBReclaimUntilShutdown() {
        let rig = SessionRig()
        let session = rig.makeSession(ledMode: .hostFeedback)
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        rig.sendFeedback([.controlChange(channel: MIDIChannel(16), controller: 20, value: 127)])
        XCTAssertTrue(rig.wait { $0.lastLEDState?.isSwitchLEDOn(index: 1) == true })
        session.stop()
        XCTAssertTrue(rig.wait { $0.statuses.last == .stopped && $0.activeConnections == 0 })
        let writesWhileStopped = rig.snapshot().ledWrites
        rig.sendFeedback([.controlChange(channel: MIDIChannel(16), controller: 21, value: 127)])
        XCTAssertTrue(rig.wait { $0.feedbackStates.last?.knownSwitches == [1: true, 2: true] })
        XCTAssertEqual(rig.snapshot().ledWrites, writesWhileStopped)
        let commandsBeforeRestart = rig.snapshot().commands.count
        session.start()
        XCTAssertTrue(rig.wait { $0.attempts == 2 && $0.statuses.last == .connected })
        XCTAssertEqual(
            rig.snapshot().commands.dropFirst(commandsBeforeRestart).prefix(4).compactMap(\.first),
            [0x05, 0x0b, 0x04, 0x05])
        XCTAssertTrue(rig.snapshot().ledStatesAtConnection.last?.isSwitchLEDOn(index: 1) == true)
        XCTAssertTrue(rig.snapshot().ledStatesAtConnection.last?.isSwitchLEDOn(index: 2) == true)
        session.suspend()
        XCTAssertTrue(rig.wait { $0.statuses.last == .sleeping && $0.activeConnections == 0 })
        rig.sendFeedback([.controlChange(channel: MIDIChannel(16), controller: 20, value: 0)])
        XCTAssertTrue(rig.wait { $0.feedbackStates.last?.knownSwitches == [1: false, 2: true] })
        session.resume()
        XCTAssertTrue(rig.wait { $0.attempts == 3 && $0.statuses.last == .connected })
        rig.disconnect()
        XCTAssertTrue(rig.wait { $0.attempts == 4 && $0.statuses.last == .connected })
        XCTAssertFalse(rig.snapshot().ledStatesAtConnection.last?.isSwitchLEDOn(index: 1) == true)
        XCTAssertTrue(rig.snapshot().ledStatesAtConnection.last?.isSwitchLEDOn(index: 2) == true)
        XCTAssertTrue(rig.snapshot().sentMessages.isEmpty)
        XCTAssertEqual(rig.snapshot().sourcesCreated, 1)
        XCTAssertEqual(rig.snapshot().feedbackDestinationsCreated, 1)
        XCTAssertEqual(rig.snapshot().sourcesDisposed, 0)
        XCTAssertEqual(rig.snapshot().feedbackDestinationsDisposed, 0)
        session.shutdown()
        XCTAssertTrue(session.waitUntilShutdown(timeout: 2))
        XCTAssertEqual(rig.snapshot().sourcesDisposed, 1)
        XCTAssertEqual(rig.snapshot().feedbackDestinationsDisposed, 1)
        XCTAssertEqual(rig.snapshot().lastLEDState, RigKontrolLEDState())
    }

    func testFailedHostStateRestorationNeverReportsConnectedAndKeepsPorts() {
        let rig = SessionRig(shortLEDWriteAttempts: [5])
        let session = rig.makeSession(retryInterval: 60, ledMode: .hostFeedback)
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        rig.sendFeedback([.controlChange(channel: MIDIChannel(16), controller: 20, value: 127)])
        XCTAssertTrue(rig.wait { $0.lastLEDState?.isSwitchLEDOn(index: 1) == true })
        session.stop()
        XCTAssertTrue(rig.wait { $0.statuses.last == .stopped && $0.activeConnections == 0 })
        session.start()
        XCTAssertTrue(rig.wait { state in
            guard case .failed(let detail) = state.statuses.last else { return false }
            return detail.contains("LED write") && detail.contains("63/64 bytes")
        })
        XCTAssertEqual(rig.snapshot().statuses.filter { $0 == .connected }.count, 1)
        XCTAssertEqual(rig.snapshot().feedbackStates.last?.knownSwitches, [1: true])
        XCTAssertEqual(rig.snapshot().sourcesCreated, 1)
        XCTAssertEqual(rig.snapshot().feedbackDestinationsCreated, 1)
        XCTAssertTrue(rig.snapshot().sentMessages.isEmpty)
    }

    func testPressOnlySendsOne127PerCycleAndLeavesExpressionAndHostLEDsIndependent() {
        let rig = SessionRig()
        let session = rig.makeSession(switchOutputMode: .pressOnly, ledMode: .hostFeedback)
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        rig.sendFeedback([.controlChange(channel: MIDIChannel(16), controller: 20, value: 127)])
        XCTAssertTrue(rig.wait { $0.lastLEDState?.isSwitchLEDOn(index: 1) == true })
        let ledWrites = rig.snapshot().ledWrites
        rig.enqueue([
            [0x04, 0x10], [0x04, 0], [0x04, 0x10], [0x04, 0],
            [0x03, 0, 0, 0, 0, 0, 100],
        ])
        XCTAssertTrue(rig.wait { $0.processedPackets >= 6 })
        XCTAssertEqual(rig.snapshot().sentMessages, [
            .controlChange(channel: MIDIChannel(16), controller: 20, value: 127),
            .controlChange(channel: MIDIChannel(16), controller: 20, value: 127),
            .controlChange(channel: MIDIChannel(16), controller: 11, value: 7),
        ])
        XCTAssertEqual(rig.snapshot().feedbackStates.last?.knownSwitches, [1: true])
        XCTAssertEqual(rig.snapshot().ledWrites, ledWrites)
    }

    func testPressOnlyReconnectAfterLostReleaseDoesNotInjectNeutralZero() {
        let rig = SessionRig(snapshotMasks: [0, 0x10])
        let session = rig.makeSession(switchOutputMode: .pressOnly)
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        rig.enqueue([[0x04, 0x10]])
        XCTAssertTrue(rig.wait { $0.sentMessages.count == 1 })
        rig.disconnect()
        XCTAssertTrue(rig.wait { $0.attempts == 2 && $0.statuses.last == .connected })
        rig.enqueue([[0x04, 0x10], [0x04, 0]])
        XCTAssertTrue(rig.wait { $0.processedPackets >= 5 })
        XCTAssertEqual(rig.snapshot().sentMessages.count, 1)
        rig.enqueue([[0x04, 0x10], [0x04, 0]])
        XCTAssertTrue(rig.wait { $0.processedPackets >= 7 })
        XCTAssertEqual(rig.snapshot().sentMessages.map { $0.bytes }, [[0xbf, 20, 127], [0xbf, 20, 127]])
        XCTAssertEqual(rig.snapshot().sourcesCreated, 1)
    }

    func testChangingOutputModeReclaimsFreshHeldSnapshotWithoutSendingMIDI() {
        let rig = SessionRig(snapshotMasks: [0, 0x10])
        let session = rig.makeSession()
        defer { session.shutdown(); session.waitUntilShutdown(timeout: 2) }
        session.start()
        XCTAssertTrue(rig.wait { $0.statuses.last == .connected })
        rig.enqueue([[0x04, 0x10]])
        XCTAssertTrue(rig.wait { $0.sentMessages.count == 1 })
        session.setSwitchOutputMode(.pressOnly)
        XCTAssertTrue(rig.wait { $0.attempts == 2 && $0.statuses.last == .connected })
        XCTAssertEqual(rig.snapshot().sentMessages.count, 1)
        session.setSwitchOutputMode(.pressOnly)
        rig.enqueue([[0x04, 0x10], [0x04, 0]])
        XCTAssertTrue(rig.wait { $0.processedPackets >= 5 })
        XCTAssertEqual(rig.snapshot().sentMessages.count, 1)
        rig.enqueue([[0x04, 0x10], [0x04, 0]])
        XCTAssertTrue(rig.wait { $0.processedPackets >= 7 })
        XCTAssertEqual(rig.snapshot().sentMessages.map { $0.bytes }, [[0xbf, 20, 127], [0xbf, 20, 127]])
        XCTAssertEqual(rig.snapshot().attempts, 2)
        XCTAssertEqual(rig.snapshot().sourcesCreated, 1)
        XCTAssertEqual(rig.snapshot().feedbackDestinationsCreated, 1)
    }
}

/// A transport that can disappear, refuse a claim, and block until cancellation.
/// It tracks actual resource ownership instead of reproducing the session state machine.
private final class SessionRig: @unchecked Sendable {
    enum Outcome { case missing, busy }
    struct State {
        var attempts = 0
        var activeConnections = 0
        var maximumConnections = 0
        var sourcesCreated = 0
        var sourcesDisposed = 0
        var feedbackDestinationsCreated = 0
        var feedbackDestinationsDisposed = 0
        var ledWrites = 0
        var queryWrites = 0
        var processedPackets = 0
        var sendAttempts = 0
        var commands: [[UInt8]] = []
        var statuses: [RigKontrolControllerStatus] = []
        var sentMessages: [MIDIMessage] = []
        var reportedMessages: [MIDIMessage] = []
        var rawExpressions: [Int] = []
        var feedbackStates: [RigKontrolLEDFeedbackState] = []
        var feedbackMessages: [MIDIMessage] = []
        var ledWriteOnMainThread = false
        var ledStatesAtConnection: [RigKontrolLEDState] = []
        var lastLEDState: RigKontrolLEDState? {
            commands.last(where: { $0.first == 0x05 }).map {
                RigKontrolLEDState(payloadBytes: Array($0.dropFirst()))
            }
        }
    }
    private let condition = NSCondition()
    private var state = State()
    private var packets: [[UInt8]] = []
    private var disconnectRequested = false
    private var initialOutcomes: [Outcome]
    private let alwaysMissing: Bool
    private let shortLEDWrites: Bool
    private let failMIDISends: Bool
    private let failMIDISendAttempts: Set<Int>
    private let automaticallyReplyToQuery: Bool
    private let shortQueryWrites: Bool
    private let shortAutomaticControlWrites: Bool
    private let primingPacket: [UInt8]?
    private var snapshotMasks: [UInt8]
    private var feedbackReceiver: (@Sendable (MIDIMessage) -> Void)?
    private let shortLEDWriteAttempts: Set<Int>

    init(
        initialOutcomes: [Outcome] = [], alwaysMissing: Bool = false,
        shortLEDWrites: Bool = false, shortLEDWriteAttempts: Set<Int> = [],
        failMIDISends: Bool = false,
        failMIDISendAttempts: Set<Int> = [],
        automaticallyReplyToQuery: Bool = true, shortQueryWrites: Bool = false,
        shortAutomaticControlWrites: Bool = false,
        primingPacket: [UInt8]? = nil,
        snapshotMasks: [UInt8] = [0]
    ) {
        self.initialOutcomes = initialOutcomes
        self.alwaysMissing = alwaysMissing
        self.shortLEDWrites = shortLEDWrites
        self.shortLEDWriteAttempts = shortLEDWriteAttempts
        self.failMIDISends = failMIDISends
        self.failMIDISendAttempts = failMIDISendAttempts
        self.automaticallyReplyToQuery = automaticallyReplyToQuery
        self.shortQueryWrites = shortQueryWrites
        self.shortAutomaticControlWrites = shortAutomaticControlWrites
        self.primingPacket = primingPacket
        self.snapshotMasks = snapshotMasks
    }

    func makeSession(
        retryInterval: TimeInterval = 0.01,
        expressionCalibration: ExpressionCalibration = ExpressionCalibration(),
        switchOutputMode: RigKontrolSwitchOutputMode = .momentary,
        ledMode: RigKontrolLEDMode = .press
    ) -> RigKontrolControllerSession {
        RigKontrolControllerSession(
            dependencies: RigKontrolControllerSessionDependencies(
                makeMIDISource: { self.change { $0.sourcesCreated += 1 }; return TestSource(rig: self) },
                makeFeedbackDestination: { received in
                    self.condition.lock()
                    self.state.feedbackDestinationsCreated += 1
                    self.feedbackReceiver = received
                    self.condition.broadcast()
                    self.condition.unlock()
                    return TestFeedbackDestination(rig: self)
                },
                stream: { try self.stream($0, $1, $2, $3) }
            ),
            retryInterval: retryInterval,
            statusChanged: { status in
                self.change {
                    $0.statuses.append(status)
                    if status == .connected,
                        let command = $0.commands.last(where: { $0.first == 0x05 })
                    {
                        $0.ledStatesAtConnection.append(RigKontrolLEDState(payloadBytes: Array(command.dropFirst())))
                    }
                }
            },
            messageSent: { message in self.change { $0.reportedMessages.append(message) } },
            rawExpressionChanged: { raw in self.change { $0.rawExpressions.append(raw) } },
            expressionCalibration: expressionCalibration,
            switchOutputMode: switchOutputMode,
            ledMode: ledMode,
            feedbackChanged: { feedback in self.change { $0.feedbackStates.append(feedback) } },
            feedbackMessageReceived: { message in self.change { $0.feedbackMessages.append(message) } }
        )
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

    func enqueue(_ newPackets: [[UInt8]]) {
        condition.lock()
        packets.append(contentsOf: newPackets)
        condition.broadcast()
        condition.unlock()
    }

    func disconnect() {
        condition.lock()
        disconnectRequested = true
        condition.broadcast()
        condition.unlock()
    }

    func sendFeedback(_ messages: [MIDIMessage]) {
        condition.lock()
        let receive = feedbackReceiver
        condition.unlock()
        for message in messages { receive?(message) }
    }

    private func stream(
        _ didOpen: (LibUSBBulkOutputWriter) -> Void,
        _ willClose: (LibUSBBulkOutputWriter) -> Void,
        _ shouldContinue: () -> Bool,
        _ packetHandler: (LibUSBBulkReadPacket, LibUSBBulkOutputWriter) -> Void
    ) throws -> LibUSBBulkStreamSummary {
        condition.lock()
        state.attempts += 1
        let outcome = initialOutcomes.isEmpty ? nil : initialOutcomes.removeFirst()
        condition.broadcast()
        condition.unlock()
        if alwaysMissing || outcome == .missing { throw LibUSBBulkReadError.deviceNotFound }
        if outcome == .busy {
            return summary(claim: "LIBUSB_ERROR_BUSY", errors: [])
        }
        change {
            $0.activeConnections += 1
            $0.maximumConnections = max($0.maximumConnections, $0.activeConnections)
        }
        defer { change { $0.activeConnections -= 1 } }
        let writer = LibUSBBulkOutputWriter(endpointAddress: 1) { bytes, _ in
            self.condition.lock()
            let isQuery = bytes == [0x04]
            let isAutomaticControlCommand = bytes.first == 0x0b
            self.state.commands.append(bytes)
            if isQuery {
                self.state.queryWrites += 1
                if self.automaticallyReplyToQuery, !self.shortQueryWrites {
                    let mask = self.snapshotMasks.first ?? 0
                    if self.snapshotMasks.count > 1 { self.snapshotMasks.removeFirst() }
                    self.packets.append([0x04, mask, 0, 0, 0, 0, 0, 0])
                }
            } else if !isAutomaticControlCommand {
                self.state.ledWrites += 1
                self.state.ledWriteOnMainThread = self.state.ledWriteOnMainThread || Thread.isMainThread
            }
            let ledAttempt = self.state.ledWrites
            self.condition.broadcast()
            self.condition.unlock()
            let isShort = isQuery ? self.shortQueryWrites
                : (isAutomaticControlCommand ? self.shortAutomaticControlWrites
                    : self.shortLEDWrites || self.shortLEDWriteAttempts.contains(ledAttempt))
            return LibUSBBulkWriteResult(
                status: "LIBUSB_SUCCESS", transferred: bytes.count - (isShort ? 1 : 0))
        }
        let bufferedPacket = primingPacket.map {
            LibUSBBulkReadPacket(endpointAddress: 0x81, timestamp: Date(), bytes: $0)
        }
        didOpen(writer)
        if let bufferedPacket, shouldContinue() {
            packetHandler(bufferedPacket, writer)
            change { $0.processedPackets += 1 }
        }
        var disconnected = false
        while shouldContinue() {
            condition.lock()
            disconnected = disconnectRequested
            if disconnected { disconnectRequested = false }
            let packet = packets.isEmpty ? nil : packets.removeFirst()
            if packet == nil, !disconnected {
                _ = condition.wait(until: Date().addingTimeInterval(0.01))
            }
            condition.unlock()
            if disconnected { break }
            if let packet {
                packetHandler(LibUSBBulkReadPacket(endpointAddress: 0x81, timestamp: Date(), bytes: packet), writer)
                change { $0.processedPackets += 1 }
            }
        }
        willClose(writer)
        return summary(claim: "LIBUSB_SUCCESS", errors: disconnected ? ["LIBUSB_ERROR_NO_DEVICE"] : [])
    }

    private func summary(claim: String, errors: [String]) -> LibUSBBulkStreamSummary {
        LibUSBBulkStreamSummary(
            openStatus: "LIBUSB_SUCCESS", configurationStatus: "LIBUSB_SUCCESS",
            claimStatus: claim, alternateSettingStatus: claim == "LIBUSB_SUCCESS" ? "LIBUSB_SUCCESS" : nil,
            releaseStatus: claim == "LIBUSB_SUCCESS" ? "LIBUSB_SUCCESS" : nil,
            packetCount: 0, nonTimeoutErrors: errors)
    }

    private final class TestSource: RigKontrolControllerMIDIOutput {
        let rig: SessionRig
        init(rig: SessionRig) { self.rig = rig }
        deinit { rig.change { $0.sourcesDisposed += 1 } }
        func send(_ message: MIDIMessage) throws {
            var attempt = 0
            rig.change { $0.sendAttempts += 1; attempt = $0.sendAttempts }
            if rig.failMIDISends || rig.failMIDISendAttempts.contains(attempt) {
                throw CoreMIDIError.osStatus(-1, operation: "test send")
            }
            rig.change { $0.sentMessages.append(message) }
        }
    }

    private final class TestFeedbackDestination: RigKontrolControllerFeedbackInput {
        let rig: SessionRig
        init(rig: SessionRig) { self.rig = rig }
        deinit {
            rig.condition.lock()
            rig.feedbackReceiver = nil
            rig.state.feedbackDestinationsDisposed += 1
            rig.condition.broadcast()
            rig.condition.unlock()
        }
    }
}
