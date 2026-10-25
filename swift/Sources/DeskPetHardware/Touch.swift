import Foundation

public struct TouchEvent: Sendable {
    public let phase: String
    public let contactId: String
    public let x: Double
    public let y: Double
    public let timestamp: Date
    public let simulated: Bool
    public init(event: HardwareEvent) throws {
        func number(_ key: String) -> Double? {
            switch event.fields[key] { case .double(let n): return n; case .int(let n): return Double(n); default: return nil }
        }
        guard event.name == "touch.event", let phase = event.fields["phase"]?.string,
              ["down", "move", "up", "cancel"].contains(phase), let contact = event.fields["contactId"]?.string,
              let x = number("x"), let y = number("y"), x.isFinite, y.isFinite, (0...1).contains(x), (0...1).contains(y),
              let ns = event.fields["timestampUnixNs"]?.integer else {
            throw HardwareError.transport("Invalid touch event")
        }
        self.phase = phase; contactId = contact; self.x = x; self.y = y
        timestamp = Date(timeIntervalSince1970: Double(ns) / 1e9)
        simulated = event.fields["simulated"] == .bool(true)
    }
}
