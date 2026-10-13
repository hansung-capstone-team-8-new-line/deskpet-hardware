#pragma once
#include "deskpet/backends.hpp"
namespace deskpet {
class Pca9685 {
  Config config_;
  std::function<void(const Bytes&)> transport_;
  void write_(uint8_t reg, uint8_t value) { transport_(Bytes{reg, value}); }
  bool enabled_ = false;
  unsigned left_ = 307, right_ = 307;
  void channel(int ch, unsigned ticks);
public:
  Pca9685(Config c, std::function<void(const Bytes&)> write)
    : config_(std::move(c)), transport_(std::move(write)) {}
  void initialize();
  void pulses(int left, int right);
  void enable();
  void disable();
};
}
