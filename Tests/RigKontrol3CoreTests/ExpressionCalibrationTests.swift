import RigKontrol3Core
import XCTest

final class ExpressionCalibrationTests: XCTestCase {
    func testDefaultCalibrationUsesCapturedExpressionRange() {
        let calibration = ExpressionCalibration()

        XCTAssertEqual(calibration.normalized(rawValue: 64), 0)
        XCTAssertEqual(calibration.normalized(rawValue: 660), 127)
    }

    func testNormalizesDefaultMinimumAndMaximum() {
        let calibration = ExpressionCalibration(rawMinimum: 64, rawMaximum: 400)

        XCTAssertEqual(calibration.normalized(rawValue: 64), 0)
        XCTAssertEqual(calibration.normalized(rawValue: 400), 127)
    }

    func testClampsOutsideRawRange() {
        let calibration = ExpressionCalibration(rawMinimum: 64, rawMaximum: 400)

        XCTAssertEqual(calibration.normalized(rawValue: 0), 0)
        XCTAssertEqual(calibration.normalized(rawValue: 512), 127)
    }

    func testMeasuredTravelReachesEndpointsDespiteObservedEndStopNoise() {
        let calibration = ExpressionCalibration.forMeasuredTravel(rawMinimum: 88, rawMaximum: 633)

        XCTAssertEqual(calibration.normalized(rawValue: 95), 0)
        XCTAssertEqual(calibration.normalized(rawValue: 625), 127)
        XCTAssertEqual(calibration.normalized(rawValue: 88), 0)
        XCTAssertEqual(calibration.normalized(rawValue: 633), 127)
        XCTAssertEqual(calibration.normalized(rawValue: 0), 0)
        XCTAssertEqual(calibration.normalized(rawValue: 1023), 127)
    }

    func testMeasuredTravelRetainsLinearMonotonicCenter() {
        let calibration = ExpressionCalibration.forMeasuredTravel(rawMinimum: 100, rawMaximum: 700)

        XCTAssertEqual(calibration.normalized(rawValue: 400), 63)
        XCTAssertEqual(calibration.normalized(rawValue: 256), 31)
        XCTAssertEqual(calibration.normalized(rawValue: 544), 95)
        let sweep = (100...700).map { calibration.normalized(rawValue: $0) }
        XCTAssertEqual(sweep, sweep.sorted())
        XCTAssertEqual(Set(sweep), Set(0...127))
    }

    func testMeasuredTravelSmallValidSpansRemainUsable() {
        let oneStep = ExpressionCalibration.forMeasuredTravel(rawMinimum: 100, rawMaximum: 101)
        XCTAssertEqual(oneStep.normalized(rawValue: 100), 0)
        XCTAssertEqual(oneStep.normalized(rawValue: 101), 127)

        let narrow = ExpressionCalibration.forMeasuredTravel(rawMinimum: 100, rawMaximum: 149)
        XCTAssertEqual(narrow, ExpressionCalibration(rawMinimum: 100, rawMaximum: 149))
    }

    func testFiltersDeadbandChanges() {
        var filter = ExpressionValueFilter(deadband: 1)

        XCTAssertEqual(filter.accept(10), 10)
        XCTAssertNil(filter.accept(11))
        XCTAssertEqual(filter.accept(12), 12)
    }

    func testExactEndpointsBypassDeadbandAndSuppressDuplicates() {
        var filter = ExpressionValueFilter(deadband: 1)

        XCTAssertEqual(filter.accept(1), 1)
        XCTAssertEqual(filter.accept(0), 0)
        XCTAssertNil(filter.accept(0))
        XCTAssertEqual(filter.accept(126), 126)
        XCTAssertEqual(filter.accept(127), 127)
        XCTAssertNil(filter.accept(127))
    }

    func testReturningFromAnEndpointRetainsDeadband() {
        var filter = ExpressionValueFilter(deadband: 1)

        XCTAssertEqual(filter.accept(0), 0)
        XCTAssertNil(filter.accept(1))
        XCTAssertEqual(filter.accept(2), 2)
        XCTAssertEqual(filter.accept(127), 127)
        XCTAssertNil(filter.accept(126))
        XCTAssertEqual(filter.accept(125), 125)
    }
}
