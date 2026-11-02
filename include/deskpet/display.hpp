#pragma once
#include "deskpet/types.hpp"
#include <optional>

namespace deskpet {
class Display {
  struct Impl;
  std::unique_ptr<Impl> impl_;
public:
  explicit Display(const Config&);
  ~Display();
  void submit(Packet command, Emit emit);
  bool tick(Emit emit); // Main thread only. False means the window was closed.
  void reset();
  Json health() const;
  void injectFailure();
  void snapshot(const std::string& path);
  void subscribeTouch(const Packet& command);
  void injectTouch(const Packet& command, Emit emit);
  // Speech envelope for the mouth, fed by the playback thread (thread-safe).
  void speechBegin(const Format& format);
  void speechAudio(const Bytes& pcm);
  void speechStarted(Clock::time_point when);  // when the first queued sample started playing
  void speechEnd();
  // Mouth opening 0..1 at `now`, or nullopt when no speech is playing.
  std::optional<float> speechLevel(Clock::time_point now) const;
};
}
