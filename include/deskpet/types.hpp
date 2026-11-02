#pragma once
#include "json.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace deskpet {
using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;
using Clock = std::chrono::steady_clock;
struct Error : std::runtime_error {
  std::string code;
  Error(std::string c, std::string message) : std::runtime_error(message), code(std::move(c)) {}
};
struct Packet { Json meta; Bytes data; uint64_t session = 0; };
using Emit = std::function<bool(Packet)>;
struct Format {
  int rate = 16000;
  int channels = 1;
  void validate() const;
  size_t frameBytes() const { return static_cast<size_t>(channels * 2); }
  size_t bytesPerSecond() const { return static_cast<size_t>(rate) * frameBytes(); }
  Json json() const { return {{"sampleRate", rate}, {"channels", channels}, {"encoding", "s16le"}}; }
  static Format from(const Json& j);
};
struct Ear {
  int channel = 0, minUs = 1000, neutralUs = 1500, maxUs = 2000;
  bool inverted = false;
  int pulse(double normalized) const;
};
struct Config {
  bool mock = true, headless = false, fullscreen = false, calibrated = false;
  std::string socketPath, font, captureDevice, playbackDevice, wavInput;
  std::string i2cDevice, oeMode = "grounded";
  double playbackGain = 0.25;
  int width = 1024, height = 600, rotation = 0, fontSize = 36;
  int dotSize = 8; // Face dot pitch in px; text screens stay vector.
  bool whiskers = true, glow = true;
  std::string mouth = "always"; // none | speaking | always
  int lipSyncDelayMs = 0; // Moves the mouth later than the PCM timeline to match device latency.
  int address = 0x40, frequency = 50, stepUs = 20;
  int leaseMs = 3000;
  Ear left, right{1};
  void validate() const;
  static Config load(const std::string& file);
};
inline int64_t unixNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}
Json identifiers(const Json& meta);
Packet reply(const Packet& command, Json result = Json::object());
Packet failure(const Packet& command, const std::string& code, const std::string& message);
Packet event(const Packet& command, const std::string& name, Json fields = Json::object(), Bytes data = {});
std::string requiredString(const Json& j, const char* key);
}
