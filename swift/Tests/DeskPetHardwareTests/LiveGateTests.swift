import XCTest
@testable import DeskPetHardware

final class LiveGateTests: XCTestCase {
    func testPlaybackStopAndFreshCapture() async throws {
        guard let path = ProcessInfo.processInfo.environment["DESKPET_TEST_SOCKET"] else {
            throw XCTSkip("Set DESKPET_TEST_SOCKET to a running mock daemon")
        }
        let client = try await HardwareClient.connect(path: path)
        defer { client.close() }
        let events = Task { do { for try await _ in client.events {} } catch {} }
        defer { events.cancel() }
        try await client.startCapture()
        let before = try await client.nextFrame()
        XCTAssertNotNil(before)
        let oldEpoch = client.inputEpoch
        let speaker = HardwareSpeakerAdapter(client: client)
        try await speaker.begin(jobId: "gate-test", event: OutputEvent(requestId: "r", outputId: "o"))
        XCTAssertFalse(client.isCurrentInputEpoch(oldEpoch))
        try await speaker.write(PCMChunk(data: Data(repeating: 0, count: 16000)), jobId: "gate-test")
        try await speaker.stop(jobId: "gate-test")
        let stopped = DispatchTime.now().uptimeNanoseconds
        let fresh = try await client.nextFrame()
        XCTAssertGreaterThanOrEqual(DispatchTime.now().uptimeNanoseconds - stopped, 290_000_000)
        XCTAssertEqual(fresh?.inputEpoch, client.inputEpoch)
        XCTAssertTrue(client.isCurrentInputEpoch(client.inputEpoch))
        XCTAssertFalse(client.isCurrentInputEpoch(oldEpoch))
        try await client.stopCapture()
        try await speaker.begin(jobId: "stop-failure", event: OutputEvent(requestId: "r2", outputId: "o2"))
        try await speaker.write(PCMChunk(data: Data(repeating: 0, count: 16000)), jobId: "stop-failure")
        _ = try await client.command("mock.fail", fields: ["device": .string("speaker-stop")])
        do {
            try await speaker.stop(jobId: "stop-failure")
            XCTFail("Unconfirmed stop must throw")
        } catch HardwareError.device(let code, _) {
            XCTAssertEqual(code, "AUDIO_STOP_UNCONFIRMED")
        }
        try await Task.sleep(nanoseconds: 400_000_000)
        XCTAssertFalse(client.isCurrentInputEpoch(client.inputEpoch))
    }
}
