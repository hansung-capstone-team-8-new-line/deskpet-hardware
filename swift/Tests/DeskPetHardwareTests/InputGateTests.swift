import XCTest
@testable import DeskPetHardware

final class InputGateTests: XCTestCase {
    func testTailAndLateFramesAndFinals() {
        var gate = InputGate()
        let old = gate.epoch
        gate.begin("one")
        XCTAssertFalse(gate.accepts(epoch: gate.epoch, capturedAt: 100, now: 100))
        gate.end("one", confirmed: true, now: 100)
        XCTAssertFalse(gate.accepts(epoch: gate.epoch, capturedAt: 300_000_099, now: 300_000_099))
        XCTAssertTrue(gate.accepts(epoch: gate.epoch, capturedAt: 300_000_100, now: 300_000_100))
        XCTAssertFalse(gate.accepts(epoch: old, capturedAt: 400_000_000, now: 400_000_000))
        XCTAssertFalse(gate.accepts(epoch: gate.epoch, capturedAt: 50, now: 400_000_000))
    }
    func testOverlappingJobsAndUnconfirmedStopFailClosed() {
        var gate = InputGate()
        gate.begin("one"); gate.begin("two")
        gate.end("one", confirmed: true, now: 0)
        XCTAssertFalse(gate.accepts(epoch: gate.epoch, capturedAt: 1_000_000_000, now: 1_000_000_000))
        gate.end("two", confirmed: false, now: 0)
        gate.begin("three"); gate.end("three", confirmed: true, now: 0)
        XCTAssertFalse(gate.accepts(epoch: gate.epoch, capturedAt: 1_000_000_000, now: 1_000_000_000))
    }
    func testDisconnectInvalidatesAndTailExtends() {
        var gate = InputGate()
        gate.begin("one"); gate.end("one", confirmed: true, now: 0)
        gate.begin("two"); gate.end("two", confirmed: true, now: 200_000_000)
        XCTAssertFalse(gate.accepts(epoch: gate.epoch, capturedAt: 400_000_000, now: 400_000_000))
        XCTAssertTrue(gate.accepts(epoch: gate.epoch, capturedAt: 500_000_000, now: 500_000_000))
        gate.invalidate()
        XCTAssertFalse(gate.accepts(epoch: gate.epoch, capturedAt: 1_000_000_000, now: 1_000_000_000))
    }
    func testCaptureRestartRejectsOldFinalWithoutReopeningPlayback() {
        var gate = InputGate()
        let previous = gate.epoch
        gate.resetCapture()
        XCTAssertFalse(gate.accepts(epoch: previous, capturedAt: 100, now: 100))
        XCTAssertTrue(gate.accepts(epoch: gate.epoch, capturedAt: 100, now: 100))
        gate.begin("playing"); gate.resetCapture()
        XCTAssertFalse(gate.accepts(epoch: gate.epoch, capturedAt: 100, now: 100))
    }
}
