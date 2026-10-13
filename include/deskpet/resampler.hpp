#pragma once
#include "deskpet/types.hpp"
#include <soxr.h>
namespace deskpet {
class SpeakerPCM {
  Format input_; double gain_; soxr_t state_ = nullptr; bool finished_ = false;
public:
  SpeakerPCM(Format input, double gain);
  ~SpeakerPCM();
  SpeakerPCM(const SpeakerPCM&) = delete;
  SpeakerPCM& operator=(const SpeakerPCM&) = delete;
  // Empty input flushes the streaming filter exactly once.
  Bytes convert(const Bytes& input);
};
}
