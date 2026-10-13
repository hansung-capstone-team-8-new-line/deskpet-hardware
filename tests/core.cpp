#include "deskpet/backends.hpp"
#include "deskpet/protocol.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace deskpet;
static void expectError(const std::function<void()>& f, const std::string& code) {
  try { f(); throw std::runtime_error("Expected error: " + code); }
  catch (const Error& e) { if (e.code != code) throw; }
}
int main() {
  Packet original{{{"version", 1}, {"text", "듣고 있어요"}}, {0, 128, 255}, 0};
  auto wire = encode(original); Decoder decoder; std::vector<Packet> decoded;
  for (auto byte : wire) { auto p = decoder.feed({&byte, 1}); decoded.insert(decoded.end(), p.begin(), p.end()); }
  if (decoded.size() != 1 || decoded[0].meta != original.meta || decoded[0].data != original.data) return 1;
  expectError([&]{ uint8_t b[]{0, 0, 64, 1, 0, 0, 0, 0}; decoder.feed(b); }, "MESSAGE_TOO_LARGE");
  expectError([]{ Format{96000, 1}.validate(); }, "INVALID_FORMAT");
  expectError([]{ Format::from({{"sampleRate", 16000}, {"channels", 1}, {"encoding", "float32"}}); }, "INVALID_FORMAT");
  Ear ear; ear.minUs = 1400; ear.maxUs = 1600;
  if (ear.pulse(-1) != 1400 || ear.pulse(0) != 1500 || ear.pulse(1) != 1600) return 2;
  ear.inverted = true; if (ear.pulse(1) != 1400) return 3;
  expectError([&]{ ear.pulse(1.1); }, "MOTION_RANGE");
  expectError([&]{ ear.pulse(std::numeric_limits<double>::quiet_NaN()); }, "MOTION_RANGE");
  if (pwmTicks(1500) != 307 || pwmTicks(1000) != 205 || pwmTicks(2000) != 410) return 4;
  expectError([]{ pwmTicks(999); }, "MOTION_RANGE");
  Config c; c.socketPath = "/tmp/test.sock"; c.validate(); c.right.channel = c.left.channel;
  expectError([&]{ c.validate(); }, "CONFIG_ERROR");
  c.right.channel = 1; std::atomic_bool cancel{false};
  auto output = makeOutput(c, Format{}); output->write(Bytes(640, 0), cancel);
  cancel = true; expectError([&]{ output->drain(cancel); }, "CANCELLED"); output->drop();
#ifdef DESKPET_LINUX
  // Exercise the real ALSA API against its null plugin. This is not physical playback.
  c.mock = false; c.playbackDevice = "null"; cancel = false;
  auto alsa = makeOutput(c, Format{});
  alsa->write(Bytes(640, 0), cancel);
  alsa->finishInput(cancel);
  if (!alsa->started()) alsa->start();
  alsa->drain(cancel);
#endif
  std::cout << "Protocol, PCM, calibration bounds and cancellation checks passed\n";
}
