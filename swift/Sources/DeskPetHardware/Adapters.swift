import Foundation

/// These adapters use this package's DTOs. Map the team's actual types at the
/// composition root; no unverified dependency on the private pipeline is shipped.
public struct HardwareMicrophoneSource: Sendable {
    private let client: HardwareClient
    public init(client: HardwareClient) { self.client = client }
    public func start() async throws { try await client.startCapture() }
    public func nextFrame() async throws -> AudioFrame? { try await client.nextFrame() }
    public func stop() async throws { try await client.stopCapture() }
}

public actor HardwareSpeakerAdapter {
    private let client: HardwareClient
    private var formats: [String: (Int, Int)] = [:]
    public init(client: HardwareClient) { self.client = client }
    public func begin(jobId: String, event: OutputEvent, sampleRate: Int = 16000, channels: Int = 1) async throws {
        var fields = event.identifiers
        fields["jobId"] = .string(jobId)
        fields["format"] = .object(["sampleRate": .int(Int64(sampleRate)), "channels": .int(Int64(channels)), "encoding": .string("s16le")])
        _ = try await client.command("speaker.begin", fields: fields)
        formats[jobId] = (sampleRate, channels)
    }
    public func write(_ chunk: PCMChunk, jobId: String) async throws {
        try chunk.validate()
        guard let expected = formats[jobId], expected.0 == chunk.sampleRate, expected.1 == chunk.channels else {
            throw HardwareError.device(code: "INVALID_FORMAT", message: "PCM format must match begin(jobId:)")
        }
        // Submit small packets so a single large chunk never exceeds the one-second queue.
        let bytesPerBlock = chunk.sampleRate / 50 * chunk.channels * 2
        var offset = 0
        while offset < chunk.data.count {
            let end = min(offset + bytesPerBlock, chunk.data.count)
            let data = chunk.data.subdata(in: offset..<end)
            let deadline = Date().addingTimeInterval(3)
            while true {
                do {
                    _ = try await client.command("speaker.write", fields: ["jobId": .string(jobId)], data: data)
                    break
                } catch HardwareError.device(let code, _) where code == "QUEUE_FULL" {
                    guard Date() < deadline else { client.close(); throw HardwareError.transport("Speaker backpressure timed out") }
                    do { try await Task.sleep(nanoseconds: 20_000_000) }
                    catch { client.close(); throw error }
                }
            }
            offset = end
        }
    }
    /// Returns only after the hardware queue has drained, never after mere acceptance.
    public func finish(jobId: String) async throws {
        defer { formats.removeValue(forKey: jobId) }
        _ = try await client.command("speaker.finish", fields: ["jobId": .string(jobId)], timeout: 8)
    }
    public func stop(jobId: String) async throws {
        defer { formats.removeValue(forKey: jobId) }
        _ = try await client.command("speaker.stop", fields: ["jobId": .string(jobId)])
    }
}

public actor HardwareDisplayAdapter {
    private let client: HardwareClient
    private var revision: Int64 = 0
    private var displaying = false
    private var waiters: [CheckedContinuation<Void, Never>] = []
    private func acquire() async throws {
        guard waiters.count < 16 else { throw HardwareError.device(code: "QUEUE_FULL", message: "Display queue full") }
        if displaying { await withCheckedContinuation { waiters.append($0) } }
        else { displaying = true }
    }
    private func release() {
        if waiters.isEmpty { displaying = false } else { waiters.removeFirst().resume() }
    }
    public init(client: HardwareClient) { self.client = client }
    public func show(_ event: OutputEvent) async throws {
        try await acquire(); defer { release() }
        try Task.checkCancellation()
        revision += 1
        var fields = event.identifiers
        fields["revision"] = .int(revision); fields["view"] = event.view
        _ = try await client.command("display.show", fields: fields)
    }
    public func idle() async throws {
        try await acquire(); defer { release() }
        try Task.checkCancellation()
        revision += 1
        _ = try await client.command("display.idle", fields: ["revision": .int(revision)])
    }
}

public struct HardwareMotionAdapter: Sendable {
    private let client: HardwareClient
    public init(client: HardwareClient) { self.client = client }
    public func enable() async throws { _ = try await client.command("motion.enable") }
    public func disable() async throws { _ = try await client.command("motion.disable") }
    public func apply(preset: String, intensity: Double = 1, requestId: String, outputId: String) async throws {
        guard intensity.isFinite, (0...1).contains(intensity) else { throw HardwareError.device(code: "MOTION_RANGE", message: "Intensity must be in 0..1") }
        _ = try await client.command("motion.set", fields: ["preset": .string(preset), "intensity": .double(intensity), "requestId": .string(requestId), "outputId": .string(outputId)])
    }
}
