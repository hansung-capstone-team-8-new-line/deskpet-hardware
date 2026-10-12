import Foundation
import DeskPetHardware

@main
struct Demo {
    static func main() async throws {
        let path = CommandLine.arguments.dropFirst().first ?? HardwareClient.defaultSocket
        let client = try await HardwareClient.connect(path: path)
        defer { client.close() }
        // Always consume delivery/error events; the client will not silently drop them.
        let events = Task {
            do { for try await event in client.events { print("event:", event.name) } } catch {}
        }
        defer { events.cancel() }
        let microphone = HardwareMicrophoneSource(client: client)
        let speaker = HardwareSpeakerAdapter(client: client)
        let display = HardwareDisplayAdapter(client: client)
        let motion = HardwareMotionAdapter(client: client)
        let id = UUID().uuidString
        try await display.show(OutputEvent(requestId: id, outputId: id, activity: "listening"))
        try await microphone.start()
        let capture = Task { () throws -> Int in
            var count = 0
            while let frame = try await microphone.nextFrame() {
                guard frame.chunk.data.count == 640 else { throw HardwareError.transport("Unexpected microphone frame size") }
                count += 1
            }
            return count
        }
        try await display.show(OutputEvent(requestId: id, outputId: id, activity: "speaking", actionStatus: "succeeded"))
        try await speaker.begin(jobId: id, event: OutputEvent(requestId: id, outputId: id))
        // Half a second of quiet 440 Hz PCM. The pipeline supplies TTS PCM here.
        for block in 0..<25 {
            var data = Data()
            for i in 0..<320 {
                let sample = Int16(1200 * sin(2 * Double.pi * 440 * Double(block * 320 + i) / 16000))
                let bits = UInt16(bitPattern: sample)
                data.append(UInt8(bits & 255)); data.append(UInt8(bits >> 8))
            }
            try await speaker.write(PCMChunk(data: data), jobId: id)
        }
        try await speaker.finish(jobId: id)
        try await microphone.stop()
        let count = try await capture.value
        guard count > 0 else { throw HardwareError.transport("No concurrent capture frames received") }
        do {
            try await motion.enable()
            try await motion.apply(preset: "wiggle", requestId: id, outputId: id)
            try await motion.disable()
        } catch { print("Motion unavailable:", error) }
        try await display.idle()
        print("Swift round trip passed; concurrent microphone frames:", count)
    }
}
