#include "deskpet/resampler.hpp"
#include <algorithm>
#include <cmath>
namespace deskpet {
SpeakerPCM::SpeakerPCM(Format input, double gain) : input_(input), gain_(gain) {
  input.validate();
  if (!std::isfinite(gain) || gain < 0 || gain > 0.25) throw Error("CONFIG_ERROR", "Playback gain must be 0..0.25");
  soxr_error_t error = nullptr;
  auto io = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
  auto quality = soxr_quality_spec(SOXR_HQ, 0);
  state_ = soxr_create(input.rate, 16000, 1, &error, &io, &quality, nullptr);
  if (error) throw Error("AUDIO_FORMAT_ERROR", error);
}
SpeakerPCM::~SpeakerPCM() { if (state_) soxr_delete(state_); }
Bytes SpeakerPCM::convert(const Bytes& bytes) {
  if (finished_) throw Error("AUDIO_OUTPUT_ERROR", "Resampler already flushed");
  if (bytes.size() % input_.frameBytes()) throw Error("INVALID_PCM", "Unaligned PCM");
  std::vector<float> mono(bytes.size() / input_.frameBytes());
  for (size_t i = 0; i < mono.size(); ++i) {
    int sum = 0;
    for (int ch = 0; ch < input_.channels; ++ch) {
      const auto offset = (i * input_.channels + ch) * 2;
      sum += int16_t(uint16_t(bytes[offset]) | (uint16_t(bytes[offset + 1]) << 8));
    }
    mono[i] = float(sum) / input_.channels;
  }
  Bytes result; size_t consumed = 0;
  do {
    float out[2048]; size_t used = 0, produced = 0;
    auto error = soxr_process(state_, bytes.empty() ? nullptr : mono.data() + consumed,
      mono.size() - consumed, &used, out, 2048, &produced);
    if (error) throw Error("AUDIO_OUTPUT_ERROR", error);
    consumed += used;
    for (size_t i = 0; i < produced; ++i) {
      // Bound filter overshoot as well as input amplitude.
      auto sample = int16_t(std::lround(std::clamp(double(out[i]), -32768.0, 32767.0) * gain_));
      result.push_back(uint16_t(sample) & 255); result.push_back(uint16_t(sample) >> 8);
    }
    if (bytes.empty() ? produced == 0 : consumed == mono.size()) break;
    if (!used && !produced) throw Error("AUDIO_OUTPUT_ERROR", "Resampler made no progress");
  } while (true);
  finished_ = bytes.empty();
  return result;
}
}
