import Foundation

public enum HardwareError: Error, Sendable, CustomStringConvertible {
    case device(code: String, message: String)
    case transport(String)
    public var description: String {
        switch self {
        case .device(let code, let message): return "\(code): \(message)"
        case .transport(let message): return message
        }
    }
}

public enum JSONValue: Codable, Sendable, Equatable {
    case string(String), int(Int64), double(Double), bool(Bool)
    case object([String: JSONValue]), array([JSONValue]), null
    public init(from decoder: Decoder) throws {
        let c = try decoder.singleValueContainer()
        if c.decodeNil() { self = .null }
        else if let v = try? c.decode(Bool.self) { self = .bool(v) }
        else if let v = try? c.decode(Int64.self) { self = .int(v) }
        else if let v = try? c.decode(Double.self) { self = .double(v) }
        else if let v = try? c.decode(String.self) { self = .string(v) }
        else if let v = try? c.decode([String: JSONValue].self) { self = .object(v) }
        else { self = .array(try c.decode([JSONValue].self)) }
    }
    public func encode(to encoder: Encoder) throws {
        var c = encoder.singleValueContainer()
        switch self {
        case .string(let v): try c.encode(v)
        case .int(let v): try c.encode(v)
        case .double(let v): try c.encode(v)
        case .bool(let v): try c.encode(v)
        case .object(let v): try c.encode(v)
        case .array(let v): try c.encode(v)
        case .null: try c.encodeNil()
        }
    }
    public var string: String? { if case .string(let v) = self { return v }; return nil }
    public var integer: Int64? { if case .int(let v) = self { return v }; return nil }
    public var object: [String: JSONValue]? { if case .object(let v) = self { return v }; return nil }
}

public struct PCMChunk: Sendable {
    public let data: Data
    public let sampleRate: Int
    public let channels: Int
    public init(data: Data, sampleRate: Int = 16000, channels: Int = 1) {
        self.data = data; self.sampleRate = sampleRate; self.channels = channels
    }
    public func validate() throws {
        guard (8000...48000).contains(sampleRate), (1...2).contains(channels), !data.isEmpty,
              data.count <= 65536, data.count % (channels * 2) == 0 else {
            throw HardwareError.device(code: "INVALID_PCM", message: "Expected frame-aligned s16le PCM, 8..48kHz, mono/stereo, at most 64 KiB")
        }
    }
}

public struct AudioFrame: Sendable {
    public let timestamp: Date
    public let sequence: Int64
    public let captureId: String
    public let chunk: PCMChunk
}

public struct OutputEvent: Sendable {
    public var requestId: String
    public var outputId: String
    public var activity: String
    public var emotion: String
    public var displayState: String
    public var displayMode: String
    public var displayText: String
    public var actionStatus: String?
    public var reminderKey: String?
    public init(requestId: String, outputId: String, activity: String = "idle", emotion: String = "neutral",
                displayState: String = "default", displayMode: String = "face", displayText: String = "",
                actionStatus: String? = nil, reminderKey: String? = nil) {
        self.requestId = requestId; self.outputId = outputId; self.activity = activity
        self.emotion = emotion; self.displayState = displayState; self.displayMode = displayMode
        self.displayText = displayText; self.actionStatus = actionStatus; self.reminderKey = reminderKey
    }
    var identifiers: [String: JSONValue] {
        var j: [String: JSONValue] = ["requestId": .string(requestId), "outputId": .string(outputId)]
        if let reminderKey { j["reminderKey"] = .string(reminderKey) }
        return j
    }
    var view: JSONValue {
        var v: [String: JSONValue] = ["activity": .string(activity), "emotion": .string(emotion),
            "displayState": .string(displayState), "displayMode": .string(displayMode), "displayText": .string(displayText)]
        if let actionStatus { v["actionStatus"] = .string(actionStatus) }
        return .object(v)
    }
}

public struct HardwareEvent: Sendable {
    public let name: String
    public let fields: [String: JSONValue]
}

struct WireMessage: Sendable {
    var meta: [String: JSONValue]
    var data: Data = Data()
    func encoded() throws -> Data {
        let json = try JSONEncoder().encode(meta)
        guard !json.isEmpty, json.count <= 16384, data.count <= 65536 else {
            throw HardwareError.transport("Message exceeds protocol limits")
        }
        var output = Data()
        for count in [json.count, data.count] {
            let n = UInt32(count)
            output.append(contentsOf: [UInt8(n >> 24), UInt8((n >> 16) & 255), UInt8((n >> 8) & 255), UInt8(n & 255)])
        }
        output.append(json); output.append(data); return output
    }
}

struct WireDecoder {
    private var buffer = Data()
    mutating func feed(_ data: Data) throws -> [WireMessage] {
        guard buffer.count + data.count <= 90120 else { throw HardwareError.transport("Receive buffer overflow") }
        buffer.append(data)
        var messages: [WireMessage] = []
        while buffer.count >= 8 {
            let bytes = Array(buffer.prefix(8))
            func length(_ i: Int) -> Int { Int(bytes[i]) << 24 | Int(bytes[i+1]) << 16 | Int(bytes[i+2]) << 8 | Int(bytes[i+3]) }
            let j = length(0), p = length(4)
            guard (1...16384).contains(j), p <= 65536 else { throw HardwareError.transport("Invalid frame lengths") }
            guard buffer.count >= 8 + j + p else { break }
            let start = buffer.startIndex
            let meta = try JSONDecoder().decode([String: JSONValue].self, from: buffer[(start+8)..<(start+8+j)])
            guard meta["version"] == .int(1) else { throw HardwareError.transport("Unsupported protocol version") }
            messages.append(WireMessage(meta: meta, data: Data(buffer[(start+8+j)..<(start+8+j+p)])))
            buffer.removeFirst(8+j+p)
        }
        return messages
    }
}
