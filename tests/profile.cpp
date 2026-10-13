#include "deskpet/pca9685.hpp"
#ifdef DESKPET_LINUX
#include "deskpet/resampler.hpp"
#endif
#include <array>
#include <cmath>
#include <iostream>
using namespace deskpet;
static void require(bool ok) { if (!ok) throw std::runtime_error("Profile regression"); }
int main() {
  Config c; c.socketPath = "/tmp/test.sock"; c.validate();
  require(c.width == 1024 && c.height == 600 && c.rotation == 0);
  c.playbackGain = 1;
  try { c.validate(); return 1; } catch (const Error&) {}
  c.playbackGain = .25;
  std::array<uint8_t, 256> regs{}; int failReg = -1;
  Pca9685 servo(c, [&](const Bytes& bytes) {
    for (size_t i = 1; i < bytes.size(); ++i) {
      int reg = bytes[0] + int(i) - 1;
      if (reg == failReg) { failReg = -1; throw Error("SERVO_ERROR", "injected"); }
      regs[reg] = bytes[i];
      if (reg >= 0xfa && reg <= 0xfd)
        for (int ch = 0; ch < 16; ++ch) regs[6 + ch * 4 + reg - 0xfa] = bytes[i];
    }
  });
  auto allOff = [&] { for (int ch = 0; ch < 16; ++ch) require(regs[9 + ch * 4] & 0x10); };
  servo.initialize(); allOff();
  servo.pulses(1400, 1600); allOff();
  servo.enable(); require(!(regs[9] & 0x10) && !(regs[13] & 0x10));
  require((regs[8] | ((regs[9] & 15) << 8)) == pwmTicks(1400));
  for (int ch = 2; ch < 16; ++ch) require(regs[9 + ch * 4] & 0x10);
  servo.disable(); allOff(); servo.pulses(1500, 1500); allOff();
  failReg = 13; try { servo.enable(); return 2; } catch (const Error&) {} allOff();
  servo.enable(); failReg = 12; try { servo.pulses(1600, 1600); return 3; } catch (const Error&) {} allOff();
  failReg = 0xfd; try { servo.disable(); return 4; } catch (const Error& e) { require(e.code == "SERVO_STOP_UNCONFIRMED"); }
#ifdef DESKPET_LINUX
  for (int rate : {8000, 16000, 22050, 24000, 44100, 48000}) for (int channels : {1, 2}) {
    Bytes input;
    for (int i = 0; i < rate; ++i) for (int ch = 0; ch < channels; ++ch) {
      auto v = uint16_t(int16_t(30000 * std::sin(2 * 3.141592653589793 * 440 * i / rate)));
      input.push_back(v & 255); input.push_back(v >> 8);
    }
    SpeakerPCM one({rate, channels}, .25), chunks({rate, channels}, .25);
    auto expected = one.convert(input); auto tail = one.convert({}); expected.insert(expected.end(), tail.begin(), tail.end());
    Bytes actual;
    const size_t block = 137 * channels * 2;
    for (size_t i = 0; i < input.size(); i += block) {
      auto out = chunks.convert(Bytes(input.begin() + i, input.begin() + std::min(input.size(), i + block)));
      actual.insert(actual.end(), out.begin(), out.end());
    }
    tail = chunks.convert({}); actual.insert(actual.end(), tail.begin(), tail.end());
    require(actual == expected && actual.size() == 32000);
    double energy = 0;
    for (size_t i = 0; i < actual.size(); i += 2) {
      int sample = int16_t(uint16_t(actual[i]) | (uint16_t(actual[i + 1]) << 8));
      require(std::abs(sample) <= 8192); energy += double(sample) * sample;
    }
    require(std::sqrt(energy / 16000) > 5000);
  }
  SpeakerPCM stereo({16000, 2}, .25);
  Bytes inverse;
  for (int i = 0; i < 16000; ++i) inverse.insert(inverse.end(), {0x10, 0x27, 0xf0, 0xd8});
  auto silence = stereo.convert(inverse); auto tail = stereo.convert({}); silence.insert(silence.end(), tail.begin(), tail.end());
  require(silence == Bytes(32000, 0));
#endif
  std::cout << "Current profile: display, PWM stop/fault/preload, streaming resampling and gain passed\n";
}
