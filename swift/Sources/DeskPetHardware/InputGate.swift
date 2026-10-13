import Foundation

/// Access under HardwareClient's lock. Keep capture running; discard gated frames.
struct InputGate {
    private(set) var epoch: Int64 = 0
    private var jobs: Set<String> = []
    private var uncertain = false
    private var resumeAfter: UInt64 = 0
    @discardableResult mutating func begin(_ job: String) -> Bool {
        guard !jobs.contains(job) else { return false }
        epoch += 1
        jobs.insert(job)
        return true
    }
    mutating func end(_ job: String, confirmed: Bool, now: UInt64) {
        guard jobs.remove(job) != nil else { return }
        if !confirmed { uncertain = true }
        resumeAfter = max(resumeAfter, now + 300_000_000)
    }
    mutating func invalidate() { epoch += 1; uncertain = true }
    mutating func resetCapture() { epoch += 1 }
    func accepts(epoch: Int64, capturedAt: UInt64, now: UInt64) -> Bool {
        !uncertain && jobs.isEmpty && epoch == self.epoch && now >= resumeAfter && capturedAt >= resumeAfter
    }
}
