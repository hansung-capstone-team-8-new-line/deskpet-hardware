#include "deskpet/types.hpp"
#include <cmath>
#include <fstream>
#include <limits>

namespace deskpet {
void Format::validate() const {
  if (rate < 8000 || rate > 48000 || (channels != 1 && channels != 2))
    throw Error("INVALID_FORMAT", "PCM must be s16le, 8000..48000 Hz and 1 or 2 channels");
}
Format Format::from(const Json& j) {
  if (j.value("encoding", "s16le") != "s16le") throw Error("INVALID_FORMAT", "Only s16le is supported");
  Format f{j.at("sampleRate").get<int>(), j.at("channels").get<int>()};
  f.validate(); return f;
}
int Ear::pulse(double normalized) const {
  if (!std::isfinite(normalized) || normalized < -1 || normalized > 1)
    throw Error("MOTION_RANGE", "Ear position must be between -1 and 1");
  const double n = inverted ? -normalized : normalized;
  return static_cast<int>(std::lround(neutralUs + n * (n < 0 ? neutralUs - minUs : maxUs - neutralUs)));
}
void Config::validate() const {
  if (oeMode != "grounded" || !std::isfinite(playbackGain) || playbackGain < 0 || playbackGain > 0.25)
    throw Error("CONFIG_ERROR", "Require grounded OE and playbackGain in 0..0.25");
  if (socketPath.empty() || socketPath.size() >= 100) throw Error("CONFIG_ERROR", "Socket path is missing or too long");
  if (width < 240 || height < 240 || width > 4096 || height > 4096 ||
      (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) || fontSize < 16 || fontSize > 128)
    throw Error("CONFIG_ERROR", "Invalid display dimensions, rotation or font size");
  if (dotSize < 4 || dotSize > 16 || (mouth != "none" && mouth != "speaking" && mouth != "always"))
    throw Error("CONFIG_ERROR", "Face dotSize must be 4..16 and mouth none, speaking or always");
  if (address < 0x03 || address > 0x77 || frequency != 50 || stepUs < 1 || stepUs > 50 || leaseMs < 1000 || leaseMs > 30000)
    throw Error("CONFIG_ERROR", "Invalid servo or connection lease settings");
  for (auto e : {left, right}) {
    if (e.channel < 0 || e.channel > 15 || e.minUs < 1000 || e.maxUs > 2000 ||
        e.minUs >= e.neutralUs || e.neutralUs >= e.maxUs)
      throw Error("CONFIG_ERROR", "Calibration must satisfy 1000 <= min < neutral < max <= 2000 us");
  }
  if (left.channel == right.channel) throw Error("CONFIG_ERROR", "Ear channels must differ");
}
Config Config::load(const std::string& file) {
  Config c;
  if (!file.empty()) {
    std::ifstream in(file);
    if (!in) throw Error("CONFIG_ERROR", "Cannot read configuration: " + file);
    Json j; in >> j;
    c.mock = j.value("mode", "mock") == "mock";
    if (j.value("mode", "mock") != "mock" && j.value("mode", "mock") != "real")
      throw Error("CONFIG_ERROR", "Mode must be mock or real");
    c.socketPath = j.value("socketPath", "");
    c.leaseMs = j.value("leaseMs", 3000);
    const auto d = j.value("display", Json::object());
    c.headless = d.value("headless", false); c.fullscreen = d.value("fullscreen", false);
    c.width = d.value("width", 1024); c.height = d.value("height", 600);
    c.rotation = d.value("rotation", 0); c.font = d.value("font", ""); c.fontSize = d.value("fontSize", 36);
    const auto f = d.value("face", Json::object());
    c.dotSize = f.value("dotSize", 8); c.whiskers = f.value("whiskers", true);
    c.glow = f.value("glow", true); c.mouth = f.value("mouth", "always");
    const auto a = j.value("audio", Json::object());
    c.captureDevice = a.value("captureDevice", ""); c.playbackDevice = a.value("playbackDevice", ""); c.wavInput = a.value("wavInput", "");
    c.playbackGain = a.value("playbackGain", 0.25);
    const auto s = j.value("servo", Json::object());
    c.i2cDevice = s.value("i2cDevice", ""); c.address = s.value("address", 64);
    c.oeMode = s.value("oeMode", "grounded");
    if (s.contains("gpioLine") || s.contains("gpioChipLabel"))
      throw Error("CONFIG_ERROR", "Legacy GPIO OE config: migrate explicitly to grounded wiring");
    c.calibrated = s.value("calibrated", false); c.frequency = s.value("frequency", 50); c.stepUs = s.value("stepUs", 20);
    auto ear = [](const Json& x, Ear& e) {
      e.channel = x.value("channel", e.channel); e.minUs = x.value("minUs", 1000);
      e.neutralUs = x.value("neutralUs", 1500); e.maxUs = x.value("maxUs", 2000); e.inverted = x.value("inverted", false);
    };
    ear(s.value("left", Json::object()), c.left); ear(s.value("right", Json::object()), c.right);
  }
  return c;
}
std::string requiredString(const Json& j, const char* key) {
  const auto v = j.at(key).get<std::string>();
  if (v.empty() || v.size() > 128) throw Error("INVALID_COMMAND", std::string(key) + " must contain 1..128 bytes");
  return v;
}
Json identifiers(const Json& meta) {
  Json j = Json::object();
  for (auto key : {"requestId", "outputId", "jobId", "captureId", "reminderKey"})
    if (meta.contains(key)) j[key] = requiredString(meta, key);
  return j;
}
Packet reply(const Packet& cmd, Json result) {
  Json j = identifiers(cmd.meta);
  j.update({{"version", 1}, {"type", "reply"}, {"commandId", cmd.meta.value("commandId", "")}, {"status", "ok"}, {"result", result}});
  return {j, {}, cmd.session};
}
Packet failure(const Packet& cmd, const std::string& code, const std::string& message) {
  Json j = {{"version", 1}, {"type", "reply"}, {"commandId", cmd.meta.value("commandId", "")},
            {"status", "error"}, {"error", {{"code", code}, {"message", message}}}};
  // An invalid request must still be rejectable, even if its identifiers are malformed.
  for (auto key : {"requestId", "outputId", "jobId", "captureId", "reminderKey"})
    if (cmd.meta.contains(key) && cmd.meta[key].is_string()) j[key] = cmd.meta[key];
  return {j, {}, cmd.session};
}
Packet event(const Packet& cmd, const std::string& name, Json fields, Bytes data) {
  Json j = identifiers(cmd.meta); j.update(fields);
  j.update({{"version", 1}, {"type", "event"}, {"event", name}, {"timestampUnixNs", unixNs()}});
  return {j, std::move(data), cmd.session};
}
}
