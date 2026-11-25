#pragma once
#include "deskpet/types.hpp"
#include <array>
#include <optional>

namespace deskpet {
// A sound effect the face asked for: a name from synthesizeEffect and a pitch multiplier.
struct SoundCue { std::string name; float pitch = 1; };
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
  // Microphone level for the listening ripples, fed by the capture thread (thread-safe).
  void micLevel(const Bytes& pcm);
  // Mouth opening 0..1 at `now`, or nullopt when no speech is playing.
  std::optional<float> speechLevel(Clock::time_point now) const;
  // Ear targets (left, right; -1 down .. +1 up) that follow the face, or nullopt until the face has been drawn
  // (headless runs and builds without SDL). Thread-safe; the servo thread reads them.
  std::optional<std::array<float, 2>> ears() const;
  // Sound effects asked for since the last call (main thread; the runtime plays them).
  std::vector<SoundCue> takeSounds();
};
}
