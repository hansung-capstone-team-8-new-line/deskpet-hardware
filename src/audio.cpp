#include "deskpet/backends.hpp"
#include <algorithm>
#include <fstream>
#include <thread>

namespace deskpet {
void checkCancelled(const std::atomic_bool& cancel) {
  if (cancel) throw Error("CANCELLED", "Operation was cancelled");
}
void waitUntil(Clock::time_point deadline, const std::atomic_bool& cancel) {
  while (Clock::now() < deadline) {
    checkCancelled(cancel);
    std::this_thread::sleep_for(std::min(deadline - Clock::now(), Clock::duration(std::chrono::milliseconds(2))));
  }
  checkCancelled(cancel);
}
static uint32_t le(const uint8_t* p, int n) {
  uint32_t x = 0; for (int i = 0; i < n; ++i) x |= uint32_t(p[i]) << (8 * i); return x;
}
class MockInput final : public AudioInput {
  std::ifstream file_;
  uint32_t remaining_ = 0;
  bool wav_ = false;
  Clock::time_point next_ = Clock::now();
public:
  explicit MockInput(const Config& c) {
    if (c.wavInput.empty()) return;
    wav_ = true; file_.open(c.wavInput, std::ios::binary);
    uint8_t h[12];
    if (!file_.read(reinterpret_cast<char*>(h), 12) || std::string(reinterpret_cast<char*>(h), 4) != "RIFF" ||
        std::string(reinterpret_cast<char*>(h + 8), 4) != "WAVE") throw Error("INVALID_WAV", "Expected a RIFF WAV file");
    bool formatOK = false;
    uint8_t b[8];
    while (file_.read(reinterpret_cast<char*>(b), 8)) {
      const std::string tag(reinterpret_cast<char*>(b), 4); const uint32_t n = le(b + 4, 4);
      if (tag == "fmt ") {
        if (n < 16 || n > 4096) throw Error("INVALID_WAV", "Invalid WAV format chunk");
        Bytes f(n); if (!file_.read(reinterpret_cast<char*>(f.data()), n)) break;
        formatOK = le(f.data(), 2) == 1 && le(f.data() + 2, 2) == 1 && le(f.data() + 4, 4) == 16000 && le(f.data() + 14, 2) == 16;
        if (n & 1) file_.ignore(1);
      } else if (tag == "data") {
        if (!formatOK || n % 2) throw Error("INVALID_WAV", "Mock WAV must be 16 kHz mono signed PCM16");
        remaining_ = n; return;
      } else file_.seekg(static_cast<std::streamoff>(n) + (n & 1), std::ios::cur);
    }
    throw Error("INVALID_WAV", "Missing or truncated WAV data");
  }
  Bytes read(const std::atomic_bool& cancel) override {
    if (wav_ && remaining_ == 0) return {};
    next_ += std::chrono::milliseconds(20); waitUntil(next_, cancel);
    if (next_ < Clock::now() - std::chrono::milliseconds(100)) next_ = Clock::now();
    Bytes out(640, 0);
    if (wav_) {
      const auto n = std::min<uint32_t>(remaining_, 640);
      if (!file_.read(reinterpret_cast<char*>(out.data()), n)) throw Error("INVALID_WAV", "WAV data is truncated");
      remaining_ -= n; // Only the last partial frame is zero-padded.
    }
    return out;
  }
};
class MockOutput final : public AudioOutput {
  Format format_; Clock::time_point end_ = Clock::now();
  bool started_ = false;
public:
  explicit MockOutput(Format f) : format_(f) {}
  void write(const Bytes& data, const std::atomic_bool& cancel) override {
    checkCancelled(cancel);
    started_ = true;
    end_ = std::max(end_, Clock::now()) + std::chrono::nanoseconds(int64_t(data.size()) * 1000000000 / format_.bytesPerSecond());
    waitUntil(end_ - std::chrono::milliseconds(80), cancel);
  }
  void drain(const std::atomic_bool& cancel) override { waitUntil(end_, cancel); }
  bool started() const override { return started_; }
  void start() override { started_ = true; }
  bool drop() noexcept override { end_ = Clock::now(); return true; }
};
std::unique_ptr<AudioInput> makeInput(const Config& c) {
  if (c.mock) return std::make_unique<MockInput>(c);
#ifdef DESKPET_LINUX
  return makeAlsaInput(c);
#else
  throw Error("UNSUPPORTED_PLATFORM", "ALSA hardware backend was not built");
#endif
}
std::unique_ptr<AudioOutput> makeOutput(const Config& c, Format f) {
  f.validate(); if (c.mock) return std::make_unique<MockOutput>(f);
#ifdef DESKPET_LINUX
  return makeAlsaOutput(c, f);
#else
  throw Error("UNSUPPORTED_PLATFORM", "ALSA hardware backend was not built");
#endif
}
}
