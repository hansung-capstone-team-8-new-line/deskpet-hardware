#pragma once
#include "deskpet/types.hpp"

namespace deskpet {
struct AudioInput {
  virtual ~AudioInput() = default;
  // Empty means end-of-file for the mock WAV input, not a dropped frame.
  virtual Bytes read(const std::atomic_bool& cancel) = 0;
};
struct AudioOutput {
  virtual ~AudioOutput() = default;
  virtual void write(const Bytes&, const std::atomic_bool& cancel) = 0;
  virtual bool started() const = 0;
  virtual void start() = 0; // Start a final short clip below the prefill threshold.
  virtual void drain(const std::atomic_bool& cancel) = 0;
  virtual void drop() noexcept = 0;
};
struct ServoBus {
  virtual ~ServoBus() = default;
  virtual void initialize() = 0;
  virtual void pulses(int leftUs, int rightUs) = 0;
  virtual void enable() = 0;
  // Can throw: never claim output is disabled when an OE write failed.
  virtual void disable() = 0;
};
std::unique_ptr<AudioInput> makeInput(const Config&);
std::unique_ptr<AudioOutput> makeOutput(const Config&, Format);
std::unique_ptr<ServoBus> makeServo(const Config&);
#ifdef DESKPET_LINUX
std::unique_ptr<AudioInput> makeAlsaInput(const Config&);
std::unique_ptr<AudioOutput> makeAlsaOutput(const Config&, Format);
std::unique_ptr<ServoBus> makeLinuxServo(const Config&);
#endif
void checkCancelled(const std::atomic_bool& cancel);
void waitUntil(Clock::time_point, const std::atomic_bool& cancel);
unsigned pwmTicks(int pulseUs, int frequency = 50);
}
