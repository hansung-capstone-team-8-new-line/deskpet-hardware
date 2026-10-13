#include "deskpet/pca9685.hpp"
#include <thread>
namespace deskpet {
void Pca9685::disable() {
  enabled_ = false;
  try { write_(0xfd, 0x10); }
  catch (...) { throw Error("SERVO_STOP_UNCONFIRMED", "PCA9685 FULL_OFF write failed"); }
}
void Pca9685::initialize() {
  disable(); // First write clears retained outputs after a daemon restart.
  write_(0x00, 0x30); write_(0xfe, 121); write_(0x01, 0x04);
  write_(0xfa, 0); write_(0xfb, 0); write_(0xfc, 0);
  disable(); write_(0x00, 0x20);
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  write_(0x00, 0xa0);
}
void Pca9685::channel(int ch, unsigned tick) {
  const auto base = uint8_t(0x06 + ch * 4);
  // Preloading neutral must leave FULL_OFF set while disabled.
  // MODE2.OCH=0 applies the complete count on STOP, avoiding torn pulse values.
  transport_(Bytes{base, 0, 0, uint8_t(tick & 255), uint8_t(((tick >> 8) & 15) | (enabled_ ? 0 : 0x10))});
}
void Pca9685::pulses(int l, int r) {
  left_ = pwmTicks(l); right_ = pwmTicks(r);
  try { channel(config_.left.channel, left_); channel(config_.right.channel, right_); }
  catch (...) { disable(); throw; }
}
void Pca9685::enable() {
  // ALL_LED_OFF writes all channel registers, not an independent output gate.
  try {
    write_(uint8_t(0x09 + config_.left.channel * 4), (left_ >> 8) & 15);
    write_(uint8_t(0x09 + config_.right.channel * 4), (right_ >> 8) & 15);
    enabled_ = true;
  } catch (...) { disable(); throw; }
}
}
