import RigKontrol3Core
import XCTest

final class RigKontrolControllerProcessorTests: XCTestCase {
    func testInitialReportAndHeldReleaseDoNotEmitOrLightLEDs() {
        var processor = RigKontrolControllerProcessor()

        assertSilent(processor.process(switchPacket(0xFF)))
        assertSilent(processor.process(switchPacket(0xFF)))
        assertSilent(processor.process(switchPacket(0)))

        let nextPress = processor.process(switchPacket(0x10))
        XCTAssertEqual(nextPress.messages, [switchMessage(1, pressed: true)])
        XCTAssertTrue(nextPress.ledState.isSwitchLEDOn(index: 1))
        XCTAssertTrue(nextPress.ledsChanged)
        let nextRelease = processor.process(switchPacket(0))
        XCTAssertEqual(nextRelease.messages, [switchMessage(1, pressed: false)])
        XCTAssertEqual(nextRelease.ledState, RigKontrolLEDState())
        XCTAssertTrue(nextRelease.ledsChanged)
    }

    func testEachPhysicalSwitchMapsToMomentaryCCAndMatchingPressLED() {
        let physicalMasks: [UInt8] = [0x10, 0x20, 0x40, 0x80, 0x01, 0x02, 0x04, 0x08]
        var processor = RigKontrolControllerProcessor()
        assertSilent(processor.process(switchPacket(0)))

        for (offset, mask) in physicalMasks.enumerated() {
            let index = offset + 1
            let press = processor.process(switchPacket(mask))
            XCTAssertEqual(press.messages, [switchMessage(index, pressed: true)])
            XCTAssertTrue(press.ledsChanged)
            for led in 1...9 {
                XCTAssertEqual(press.ledState.isSwitchLEDOn(index: led), led == index)
            }
            let repeated = processor.process(switchPacket(mask))
            XCTAssertEqual(repeated.messages, [])
            XCTAssertEqual(repeated.ledState, press.ledState)
            XCTAssertFalse(repeated.ledsChanged)
            let release = processor.process(switchPacket(0))
            XCTAssertEqual(release.messages, [switchMessage(index, pressed: false)])
            XCTAssertEqual(release.ledState, RigKontrolLEDState())
            XCTAssertTrue(release.ledsChanged)
        }
    }

    func testSimultaneousAndRapidTransitionsPreserveEveryEdge() {
        var processor = RigKontrolControllerProcessor()
        _ = processor.process(switchPacket(0))
        let allPressed = processor.process(switchPacket(0xFF))
        XCTAssertEqual(allPressed.messages, (1...8).map { switchMessage($0, pressed: true) })
        let allReleased = processor.process(switchPacket(0))
        XCTAssertEqual(allReleased.messages, (1...8).map { switchMessage($0, pressed: false) })

        for _ in 0..<20 {
            XCTAssertEqual(
                processor.process(switchPacket(0x10)).messages, [switchMessage(1, pressed: true)])
            XCTAssertEqual(
                processor.process(switchPacket(0)).messages, [switchMessage(1, pressed: false)])
        }
    }

    func testNewPressIsAcceptedWhileAnInitiallyHeldSwitchIsStillHeld() {
        var processor = RigKontrolControllerProcessor()
        assertSilent(processor.process(switchPacket(0x10)))
        let nextPress = processor.process(switchPacket(0x30))
        XCTAssertEqual(nextPress.messages, [switchMessage(2, pressed: true)])
        XCTAssertFalse(nextPress.ledState.isSwitchLEDOn(index: 1))
        XCTAssertTrue(nextPress.ledState.isSwitchLEDOn(index: 2))

        let releases = processor.process(switchPacket(0))
        XCTAssertEqual(releases.messages, [switchMessage(2, pressed: false)])
        XCTAssertEqual(releases.ledState, RigKontrolLEDState())
    }

    func testFreshAttachmentDoesNotReplayPreviousPressOrRelease() {
        var firstAttachment = RigKontrolControllerProcessor()
        _ = firstAttachment.process(switchPacket(0))
        _ = firstAttachment.process(switchPacket(0x10))

        var secondAttachment = RigKontrolControllerProcessor()
        assertSilent(secondAttachment.process(switchPacket(0x10)))
        assertSilent(secondAttachment.process(switchPacket(0)))
        XCTAssertEqual(
            secondAttachment.process(switchPacket(0x10)).messages,
            [switchMessage(1, pressed: true)])
    }

    func testExpressionReachesBothEndpointsWithoutChangingLEDs() {
        var processor = RigKontrolControllerProcessor()
        let nearMinimum = processor.process(expressionPacket(69))
        XCTAssertEqual(nearMinimum.messages, [expressionMessage(1)])
        let minimum = processor.process(expressionPacket(64))
        XCTAssertEqual(minimum.messages, [expressionMessage(0)])
        assertSilent(processor.process(expressionPacket(64)))
        let nearMaximum = processor.process(expressionPacket(656))
        XCTAssertEqual(nearMaximum.messages, [expressionMessage(126)])
        let maximum = processor.process(expressionPacket(660))
        XCTAssertEqual(maximum.messages, [expressionMessage(127)])
        assertSilent(processor.process(expressionPacket(660)))
        XCTAssertFalse(nearMinimum.ledsChanged)
        XCTAssertFalse(minimum.ledsChanged)
        XCTAssertFalse(nearMaximum.ledsChanged)
        XCTAssertFalse(maximum.ledsChanged)
        XCTAssertEqual(maximum.ledState, RigKontrolLEDState())
        // Expression reports must not consume the first switch baseline.
        assertSilent(processor.process(switchPacket(0x10)))
    }

    func testUnknownAndTruncatedPacketsDoNotConsumeTheSwitchBaseline() {
        var processor = RigKontrolControllerProcessor()
        assertSilent(processor.process([]))
        assertSilent(processor.process([0x04]))
        assertSilent(processor.process([0x03, 0, 0]))
        assertSilent(processor.process([0xFF, 0x10]))
        assertSilent(processor.process(switchPacket(0x10)))
        assertSilent(processor.process(switchPacket(0)))
    }

    func testCustomExpressionCalibrationMapsActualTravelToBothEndpoints() {
        var processor = RigKontrolControllerProcessor(
            expressionCalibration: ExpressionCalibration(rawMinimum: 90, rawMaximum: 640))

        XCTAssertEqual(processor.process(expressionPacket(90)).messages, [expressionMessage(0)])
        XCTAssertEqual(processor.process(expressionPacket(640)).messages, [expressionMessage(127)])
    }

    func testChangedCalibrationResendsTheSamePhysicalValueAfterResettingFilter() {
        var processor = RigKontrolControllerProcessor()
        XCTAssertEqual(processor.process(expressionPacket(400)).messages, [expressionMessage(71)])
        XCTAssertEqual(processor.process(expressionPacket(400)).messages, [])

        processor.setExpressionCalibration(ExpressionCalibration(rawMinimum: 90, rawMaximum: 640))

        // Both ranges normalize raw 400 to 71. A changed calibration still needs
        // to publish the next sample rather than retain the old filter baseline.
        XCTAssertEqual(processor.process(expressionPacket(400)).messages, [expressionMessage(71)])
        XCTAssertEqual(processor.process(expressionPacket(400)).messages, [])
        XCTAssertEqual(processor.process(expressionPacket(90)).messages, [expressionMessage(0)])
        XCTAssertEqual(processor.process(expressionPacket(640)).messages, [expressionMessage(127)])
    }

    func testUnchangedCalibrationDoesNotResetExpressionFilter() {
        let calibration = ExpressionCalibration(rawMinimum: 90, rawMaximum: 640)
        var processor = RigKontrolControllerProcessor(expressionCalibration: calibration)
        XCTAssertEqual(processor.process(expressionPacket(400)).messages, [expressionMessage(71)])

        processor.setExpressionCalibration(calibration)

        XCTAssertEqual(processor.process(expressionPacket(400)).messages, [])
        XCTAssertEqual(processor.process(expressionPacket(406)).messages, [])
        XCTAssertEqual(processor.process(expressionPacket(411)).messages, [expressionMessage(74)])
    }

    func testChangedCalibrationPreservesSwitchBaselineSuppressionAndLEDState() {
        var processor = RigKontrolControllerProcessor()
        assertSilent(processor.process(switchPacket(0x10)))
        let secondSwitchPress = processor.process(switchPacket(0x30))
        XCTAssertEqual(secondSwitchPress.messages, [switchMessage(2, pressed: true)])

        processor.setExpressionCalibration(ExpressionCalibration(rawMinimum: 90, rawMaximum: 640))

        let held = processor.process(switchPacket(0x30))
        XCTAssertEqual(held.messages, [])
        XCTAssertEqual(held.ledState, secondSwitchPress.ledState)
        XCTAssertFalse(held.ledsChanged)
        let release = processor.process(switchPacket(0))
        XCTAssertEqual(release.messages, [switchMessage(2, pressed: false)])
        XCTAssertEqual(release.ledState, RigKontrolLEDState())
        XCTAssertTrue(release.ledsChanged)
        XCTAssertEqual(
            processor.process(switchPacket(0x10)).messages, [switchMessage(1, pressed: true)])
    }

    private func switchPacket(_ mask: UInt8) -> [UInt8] {
        [0x04, mask, 0, 0, 0, 0, 0, 0]
    }

    private func expressionPacket(_ value: Int) -> [UInt8] {
        var bytes = [UInt8](repeating: 0, count: 33)
        bytes[0] = 0x03
        bytes[5] = UInt8((value >> 8) & 0xFF)
        bytes[6] = UInt8(value & 0xFF)
        return bytes
    }

    private func switchMessage(_ index: Int, pressed: Bool) -> MIDIMessage {
        .controlChange(channel: MIDIChannel(16), controller: 19 + index, value: pressed ? 127 : 0)
    }

    private func expressionMessage(_ value: Int) -> MIDIMessage {
        .controlChange(channel: MIDIChannel(16), controller: 11, value: value)
    }

    private func assertSilent(
        _ frame: RigKontrolControllerFrame, file: StaticString = #filePath, line: UInt = #line
    ) {
        XCTAssertEqual(frame.messages, [], file: file, line: line)
        XCTAssertEqual(frame.ledState, RigKontrolLEDState(), file: file, line: line)
        XCTAssertFalse(frame.ledsChanged, file: file, line: line)
    }
}
