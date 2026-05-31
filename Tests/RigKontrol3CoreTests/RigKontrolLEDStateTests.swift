import RigKontrol3Core
import XCTest

final class RigKontrolLEDStateTests: XCTestCase {
    func testDefaultStateBuildsWriteIOCommandWithCenteredDashesAndLEDsOff() {
        let state = RigKontrolLEDState()

        XCTAssertEqual(state.writeIOCommandBytes.count, 64)
        XCTAssertEqual(state.writeIOCommandBytes[0], 0x05)
        XCTAssertEqual(state.payloadBytes[0], 0x00)
        XCTAssertEqual(state.payloadBytes[1], 0x40)
        XCTAssertEqual(state.payloadBytes[2], 0x40)
        XCTAssertEqual(state.payloadBytes[3], 0x00)
        XCTAssertEqual(state.payloadBytes[4], 0x00)
        XCTAssertEqual(state.payloadBytes[5], 0x00)
    }

    func testSwitchLEDsOneThroughEightUsePhysicalSwitchLabelMapping() {
        var state = RigKontrolLEDState()

        state.setSwitchLED(index: 1, isOn: true)
        XCTAssertEqual(state.payloadBytes[4], 0x10)
        XCTAssertTrue(state.isSwitchLEDOn(index: 1))

        state.setSwitchLED(index: 8, isOn: true)
        XCTAssertEqual(state.payloadBytes[4], 0x18)
        XCTAssertTrue(state.isSwitchLEDOn(index: 8))

        state.setSwitchLED(index: 1, isOn: false)
        XCTAssertEqual(state.payloadBytes[4], 0x08)
        XCTAssertFalse(state.isSwitchLEDOn(index: 1))
    }

    func testPedalLEDUsesPayloadByteFiveBitZero() {
        var state = RigKontrolLEDState()

        state.setSwitchLED(index: 9, isOn: true)

        XCTAssertEqual(state.payloadBytes[4], 0x00)
        XCTAssertEqual(state.payloadBytes[5], 0x01)
        XCTAssertTrue(state.isSwitchLEDOn(index: 9))
    }

    func testSwitchLEDFiveUsesPayloadByteFourBitZeroAndClears() {
        var state = RigKontrolLEDState()

        state.setSwitchLED(index: 5, isOn: true)
        XCTAssertEqual(state.payloadBytes[4], 0x01)
        XCTAssertTrue(state.isSwitchLEDOn(index: 5))

        state.setSwitchLED(index: 5, isOn: false)
        XCTAssertEqual(state.payloadBytes[4], 0x00)
        XCTAssertFalse(state.isSwitchLEDOn(index: 5))
    }
}
