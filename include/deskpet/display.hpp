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
};
}
