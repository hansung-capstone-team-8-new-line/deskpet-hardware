import Foundation
import Dispatch
#if os(Linux)
import Glibc
#else
import Darwin
#endif

/// One client owns one hardware session. close() stops its capture, speaker and ears.
/// Request cancellation closes the session: a timed-out mutation is never silently retried.
public final class HardwareClient: @unchecked Sendable {
    public let events: AsyncThrowingStream<HardwareEvent, Error>
    private let eventSink: AsyncThrowingStream<HardwareEvent, Error>.Continuation
    private let lock = NSLock()
    private let writeLock = NSLock()
    private let writer = DispatchQueue(label: "deskpet.socket.writer")
    private var fd: Int32
    private var closed = false
    private var pending: [String: CheckedContinuation<WireMessage, Error>] = [:]
    private var frames: [AudioFrame] = []
    private var inputGate = InputGate()
    public var inputEpoch: Int64 { lock.withCriticalSection { inputGate.epoch } }
    public func isCurrentInputEpoch(_ epoch: Int64) -> Bool {
        lock.withCriticalSection {
            let now = DispatchTime.now().uptimeNanoseconds
            return inputGate.accepts(epoch: epoch, capturedAt: now, now: now)
        }
    }
    private var frameWaiter: CheckedContinuation<AudioFrame?, Error>?
    private var captureError: Error?
    private var captureEnded = true
    private var captureId: String?
    private var sequence: Int64?
    private var heartbeat: DispatchSourceTimer?

    public static var defaultSocket: String {
        if let directory = ProcessInfo.processInfo.environment["XDG_RUNTIME_DIR"] { return directory + "/deskpet-hardware.sock" }
        return "/tmp/deskpet-\(getuid())/hardware.sock"
    }

    public static func connect(path: String = defaultSocket) async throws -> HardwareClient {
        let client = try HardwareClient(path: path)
        do {
            let hello = try await client.command("hello")
            let lease = hello["leaseMs"]?.integer ?? 3000
            client.startHeartbeat(seconds: max(0.1, Double(lease) / 3000))
            return client
        } catch { client.close(); throw error }
    }

    private init(path: String) throws {
        var continuation: AsyncThrowingStream<HardwareEvent, Error>.Continuation!
        events = AsyncThrowingStream(bufferingPolicy: .bufferingOldest(128)) { continuation = $0 }
        eventSink = continuation
        #if os(Linux)
        fd = socket(AF_UNIX, Int32(SOCK_STREAM.rawValue), 0)
        #else
        fd = socket(AF_UNIX, SOCK_STREAM, 0)
        #endif
        guard fd >= 0 else { throw HardwareError.transport("Cannot create Unix socket") }
        var address = sockaddr_un()
        address.sun_family = sa_family_t(AF_UNIX)
        let bytes = Array(path.utf8) + [0]
        guard bytes.count <= MemoryLayout.size(ofValue: address.sun_path) else {
            systemClose(fd); throw HardwareError.transport("Socket path is too long")
        }
        withUnsafeMutableBytes(of: &address.sun_path) { $0.copyBytes(from: bytes) }
        let result = withUnsafePointer(to: &address) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                systemConnect(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        guard result == 0 else { systemClose(fd); throw HardwareError.transport("Cannot connect to \(path): \(String(cString: strerror(errno)))") }
        #if !os(Linux)
        var one: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, socklen_t(MemoryLayout<Int32>.size))
        #endif
        var timeout = timeval(tv_sec: 2, tv_usec: 0)
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
        DispatchQueue(label: "deskpet.socket.reader").async { self.readLoop() }
    }

    private func startHeartbeat(seconds: Double) {
        let timer = DispatchSource.makeTimerSource(queue: DispatchQueue(label: "deskpet.heartbeat"))
        timer.schedule(deadline: .now() + seconds, repeating: seconds)
        timer.setEventHandler { [weak self] in
            guard let self else { return }
            Task { do { _ = try await self.command("ping", timeout: 2) } catch { self.close() } }
        }
        lock.withCriticalSection { if !closed { heartbeat = timer; timer.resume() } else { timer.setEventHandler {}; timer.resume(); timer.cancel() } }
    }

    public func command(_ command: String, fields: [String: JSONValue] = [:], data: Data = Data(), timeout: Double = 5) async throws -> [String: JSONValue] {
        try Task.checkCancellation()
        let id = UUID().uuidString
        var meta = fields
        meta["version"] = .int(1); meta["type"] = .string("command")
        meta["commandId"] = .string(id); meta["command"] = .string(command)
        let wire = try WireMessage(meta: meta, data: data).encoded()
        if command == "speaker.begin", let job = fields["jobId"]?.string {
            try lock.withCriticalSection {
                guard inputGate.begin(job) else { throw HardwareError.device(code: "BUSY", message: "Duplicate speaker job") }
                frames.removeAll()
            }
            switch eventSink.yield(HardwareEvent(name: "input.invalidated", fields: ["inputEpoch": .int(inputEpoch)])) {
            case .enqueued: break
            default: close(); throw HardwareError.transport("Input invalidation consumer unavailable")
            }
        }
        let response: WireMessage = try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                let registered = lock.withCriticalSection { () -> Bool in
                    guard !closed else { return false }
                    pending[id] = continuation; return true
                }
                guard registered else { continuation.resume(throwing: HardwareError.transport("Connection closed")); return }
                writer.async { [self] in
                    do { try sendAll(wire) } catch { fail(error) }
                }
                DispatchQueue.global().asyncAfter(deadline: .now() + timeout) { [weak self] in
                    guard let self else { return }
                    if self.lock.withCriticalSection({ self.pending[id] != nil }) { self.fail(HardwareError.transport("Timed out: \(command)")) }
                }
            }
        } onCancel: { self.fail(CancellationError()) }
        guard let status = response.meta["status"]?.string, status == "ok" || status == "error" else {
            close(); throw HardwareError.transport("Malformed reply status")
        }
        if status == "error" {
            let error = response.meta["error"]?.object ?? [:]
            if command == "speaker.begin", let job = fields["jobId"]?.string {
                // Rejection before playback, or a backend failure before any PCM write.
                lock.withCriticalSection { inputGate.end(job, confirmed: true, now: DispatchTime.now().uptimeNanoseconds) }
            }
            throw HardwareError.device(code: error["code"]?.string ?? "UNKNOWN", message: error["message"]?.string ?? "Hardware command failed")
        }
        return response.meta["result"]?.object ?? [:]
    }

    private func sendAll(_ data: Data) throws {
        try writeLock.withCriticalSection {
            guard !lock.withCriticalSection({ closed }) else { throw HardwareError.transport("Connection closed") }
            try data.withUnsafeBytes { ptr in
                var done = 0
                while done < data.count {
                    #if os(Linux)
                    let n = send(fd, ptr.baseAddress!.advanced(by: done), data.count - done, Int32(MSG_NOSIGNAL))
                    #else
                    let n = send(fd, ptr.baseAddress!.advanced(by: done), data.count - done, 0)
                    #endif
                    if n < 0 && errno == EINTR { continue }
                    guard n > 0 else { throw HardwareError.transport("Socket send failed") }
                    done += n
                }
            }
        }
    }

    private func readLoop() {
        defer { writeLock.withCriticalSection { systemClose(fd) } }
        var decoder = WireDecoder(), bytes = [UInt8](repeating: 0, count: 8192)
        do {
            while true {
                let n = recv(fd, &bytes, bytes.count, 0)
                if n < 0 && errno == EINTR { continue }
                guard n > 0 else { throw HardwareError.transport("Hardware disconnected") }
                for packet in try decoder.feed(Data(bytes.prefix(n))) { try handle(packet) }
            }
        } catch { fail(error) }
    }

    private func handle(_ packet: WireMessage) throws {
        if packet.meta["type"]?.string == "reply" {
            guard let id = packet.meta["commandId"]?.string else { throw HardwareError.transport("Reply has no commandId") }
            let waiter = lock.withCriticalSection { pending.removeValue(forKey: id) }
            waiter?.resume(returning: packet)
            return
        }
        guard let event = packet.meta["event"]?.string else { throw HardwareError.transport("Malformed event") }
        if event == "audio.frame" {
            guard let id = packet.meta["captureId"]?.string,
                  let seq = packet.meta["sequence"]?.integer, let ns = packet.meta["timestampUnixNs"]?.integer,
                  let format = packet.meta["format"]?.object, format["sampleRate"] == .int(16000), format["channels"] == .int(1),
                  format["encoding"] == .string("s16le"), packet.data.count == 640 else { throw HardwareError.transport("Invalid microphone frame") }
            guard let captured = packet.meta["captureEarliestMonotonicNs"]?.integer, captured >= 0 else {
                throw HardwareError.transport("Update daemon: capture timing bound is required for input gating")
            }
            var frame = AudioFrame(timestamp: Date(timeIntervalSince1970: Double(ns) / 1e9), sequence: seq, captureId: id, chunk: PCMChunk(data: packet.data))
            var waiter: CheckedContinuation<AudioFrame?, Error>?
            try lock.withCriticalSection {
                guard id == captureId, !captureEnded else { return }
                if let sequence, seq != sequence + 1 { throw HardwareError.device(code: "AUDIO_DISCONTINUITY", message: "Frame sequence gap") }
                sequence = seq
                guard inputGate.accepts(epoch: inputGate.epoch, capturedAt: UInt64(captured), now: DispatchTime.now().uptimeNanoseconds) else { return }
                frame.inputEpoch = inputGate.epoch
                if let waiting = frameWaiter { waiter = waiting; frameWaiter = nil }
                else {
                    guard frames.count < 50 else { throw HardwareError.device(code: "AUDIO_DISCONTINUITY", message: "Client capture queue overflow") }
                    frames.append(frame)
                }
            }
            waiter?.resume(returning: frame)
            return
        }
        if ["audio.completed", "audio.cancelled", "audio.failed"].contains(event), let job = packet.meta["jobId"]?.string {
            let confirmed = event == "audio.completed" || packet.meta["stoppedConfirmed"] == .bool(true)
            lock.withCriticalSection { inputGate.end(job, confirmed: confirmed, now: DispatchTime.now().uptimeNanoseconds); frames.removeAll() }
        }
        if event == "device.error", packet.meta["device"]?.string == "capture" {
            finishCapture(error: HardwareError.device(code: packet.meta["code"]?.string ?? "AUDIO_DEVICE_ERROR", message: packet.meta["message"]?.string ?? "Capture failed"))
        } else if event == "capture.stopped" {
            if lock.withCriticalSection({ packet.meta["captureId"]?.string == captureId }) { finishCapture(error: nil) }
        }
        switch eventSink.yield(HardwareEvent(name: event, fields: packet.meta)) {
        case .enqueued: break
        case .dropped:
            throw HardwareError.transport("Unconsumed hardware event queue overflow")
        case .terminated:
            throw HardwareError.transport("Hardware event consumer ended")
        @unknown default:
            throw HardwareError.transport("Unknown event stream state")
        }
    }

    private func finishCapture(error: Error?) {
        let waiter = lock.withCriticalSection { () -> CheckedContinuation<AudioFrame?, Error>? in
            inputGate.resetCapture()
            captureEnded = true
            if let error { captureError = error; frames.removeAll() }
            let waiter = frameWaiter; frameWaiter = nil; return waiter
        }
        if let error { waiter?.resume(throwing: error) } else { waiter?.resume(returning: nil) }
    }

    public func startCapture() async throws {
        let id = UUID().uuidString
        try lock.withCriticalSection {
            guard captureEnded else { throw HardwareError.device(code: "BUSY", message: "Capture is already active") }
            inputGate.resetCapture()
            captureId = id; captureEnded = false; captureError = nil; sequence = nil; frames.removeAll()
        }
        do { _ = try await command("capture.start", fields: ["captureId": .string(id)]) }
        catch { finishCapture(error: error); throw error }
    }
    public func stopCapture() async throws {
        let id = lock.withCriticalSection { captureId ?? "" }
        _ = try await command("capture.stop", fields: ["captureId": .string(id)])
        lock.withCriticalSection { frames.removeAll() }
    }
    public func nextFrame() async throws -> AudioFrame? {
        try Task.checkCancellation()
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                lock.lock()
                if let error = captureError { lock.unlock(); continuation.resume(throwing: error) }
                else if !frames.isEmpty { let frame = frames.removeFirst(); lock.unlock(); continuation.resume(returning: frame) }
                else if captureEnded { lock.unlock(); continuation.resume(returning: nil) }
                else if frameWaiter != nil { lock.unlock(); continuation.resume(throwing: HardwareError.transport("Only one nextFrame consumer is allowed")) }
                else { frameWaiter = continuation; lock.unlock() }
            }
        } onCancel: { self.fail(CancellationError()) }
    }
    public func close() { fail(HardwareError.transport("Client closed")) }
    private func fail(_ error: Error) {
        lock.lock()
        if closed { lock.unlock(); return }
        closed = true
        inputGate.invalidate(); frames.removeAll()
        let waiting = Array(pending.values); pending.removeAll()
        let timer = heartbeat; heartbeat = nil
        lock.unlock()
        timer?.cancel()
        _ = shutdown(fd, Int32(SHUT_RDWR))
        waiting.forEach { $0.resume(throwing: error) }
        finishCapture(error: error)
        eventSink.finish(throwing: error)
    }
}

private func systemClose(_ fd: Int32) {
    #if os(Linux)
    _ = Glibc.close(fd)
    #else
    _ = Darwin.close(fd)
    #endif
}
private func systemConnect(_ fd: Int32, _ address: UnsafePointer<sockaddr>, _ size: socklen_t) -> Int32 {
    #if os(Linux)
    return Glibc.connect(fd, address, size)
    #else
    return Darwin.connect(fd, address, size)
    #endif
}

// Swift 5.10 Linux Foundation does not expose NSLock.withLock.
private extension NSLock {
    func withCriticalSection<T>(_ body: () throws -> T) rethrows -> T {
        lock(); defer { unlock() }
        return try body()
    }
}
