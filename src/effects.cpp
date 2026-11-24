#include "deskpet/backends.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace deskpet {
namespace {
constexpr double kTau = 6.283185307179586;
double smooth01(double x) { x = std::clamp(x, 0.0, 1.0); return x * x * (3 - 2 * x); }
// One gliding tone added into `out`: from f0 to f1 (shaped by `curve`), a little second harmonic and optional vibrato.
// It rises over at least 12 ms so it never clicks, then decays quadratically.
void tone(std::vector<double>& out, int rate, double t0, double duration, double f0, double f1,
          double curve = 1, double harmonic = 0.08, double vibrato = 0) {
  const size_t n0 = size_t(std::lround(t0 * rate)), n = size_t(std::lround(duration * rate));
  const double rise = std::max(0.08 * double(n), 0.012 * rate);
  double phase = 0;
  for (size_t i = 0; i < n && n0 + i < out.size(); ++i) {
    const double u = double(i) / double(n);
    const double f = f0 + (f1 - f0) * std::pow(u, curve) + vibrato * f0 * std::sin(kTau * 7 * double(i) / rate);
    phase += kTau * f / rate;
    const double a = double(i) < rise ? smooth01(double(i) / rise) : std::pow((double(n) - double(i)) / (double(n) - rise), 2);
    out[n0 + i] += a * (std::sin(phase) + harmonic * std::sin(2 * phase));
  }
}
}

// Short sound effects made from sine tones and noise, no audio files. The base is 520 Hz (the highest effect
// stays near 1 kHz; an 880 Hz base reaching 2.6 kHz sounded harsh on the small speaker), a 2.5 kHz low-pass
// takes the edge off, and the peak is set to 0.5 x volume. Matches synth() in docs/face-catalog/index.html.
Bytes synthesizeEffect(const std::string& name, float pitch, double volume, int rate) {
  const double P = 520.0 * pitch;
  std::vector<double> out;
  const auto length = [&](double seconds) { out.assign(size_t(std::lround(seconds * rate)), 0.0); };
  if (name == "boop") { length(0.11); tone(out, rate, 0, 0.1, P, P * 0.8); }
  else if (name == "nom") { length(0.21); tone(out, rate, 0, 0.08, P, P * 0.88); tone(out, rate, 0.1, 0.09, P * 0.82, P * 0.7); }
  else if (name == "chirp") { length(0.24); tone(out, rate, 0, 0.13, P, P * 1.35, 0.7); tone(out, rate, 0.1, 0.13, P * 1.25, P * 1.6, 0.7); }
  else if (name == "pop") { length(0.13); tone(out, rate, 0, 0.12, P * 0.8, P * 1.6, 2.5, 0.04); }
  else if (name == "flutter") {
    length(0.17); tone(out, rate, 0, 0.16, P * 1.1, P, 1, 0.05);
    for (size_t i = 0; i < out.size(); ++i) out[i] *= 0.6 + 0.4 * std::sin(kTau * 28 * double(i) / rate);
  } else if (name == "mrrp") { length(0.34); tone(out, rate, 0, 0.32, P * 0.55, P * 0.8, 0.6, 0.2, 0.03); }
  else if (name == "sparkle") {
    length(0.4); tone(out, rate, 0, 0.13, P * 1.25, P * 1.25); tone(out, rate, 0.09, 0.13, P * 1.575, P * 1.575);
    tone(out, rate, 0.18, 0.2, P * 1.875, P * 1.875);
  } else if (name == "purr") {
    length(1.1);
    for (size_t i = 0; i < out.size(); ++i) {
      const double t = double(i) / rate, u = t / 1.1;
      const double envelope = std::min(1.0, u / 0.25) * std::min(1.0, (1 - u) / 0.35), am = 0.55 + 0.45 * std::sin(kTau * 24 * t);
      out[i] = envelope * am * (std::sin(kTau * 110 * t * pitch) + 0.6 * std::sin(kTau * 220 * t * pitch) + 0.3 * std::sin(kTau * 330 * t * pitch));
    }
  } else if (name == "whoosh") {
    length(0.22);
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> noise(-1, 1);
    double y = 0;
    for (size_t i = 0; i < out.size(); ++i) {
      const double u = double(i) / double(out.size()), k = 1 - std::exp(-kTau * (300 + 1500 * u) / rate);
      y += k * (noise(rng) - y); out[i] = y * std::sin(3.141592653589793 * u);
    }
  } else return {};
  const double k = 1 - std::exp(-kTau * 2500 / rate);
  double y = 0, peak = 0;
  for (auto& v : out) { y += k * (v - y); v = y; peak = std::max(peak, std::abs(y)); }
  const double scale = peak > 0 ? 0.5 * std::clamp(volume, 0.0, 1.0) / peak : 0;
  Bytes pcm(out.size() * 2);
  for (size_t i = 0; i < out.size(); ++i) {
    const auto s = int16_t(std::lround(std::clamp(out[i] * scale, -1.0, 1.0) * 32767));
    pcm[2 * i] = uint8_t(uint16_t(s) & 0xff); pcm[2 * i + 1] = uint8_t(uint16_t(s) >> 8);
  }
  return pcm;
}
}
