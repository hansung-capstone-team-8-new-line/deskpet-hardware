#pragma once
#include "deskpet/display.hpp"

namespace deskpet {
class Runtime {
  struct Impl;
  std::unique_ptr<Impl> impl_;
public:
  Runtime(const Config&, Emit);
  ~Runtime();
  void command(Packet);
  void disconnect();
  bool tick();
};
int runServer(const Config&, std::atomic_bool& interrupted);
}
