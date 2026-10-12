#include "deskpet/backends.hpp"
#include <cmath>

namespace deskpet {
unsigned pwmTicks(int pulseUs, int frequency) {
  if (pulseUs < 1000 || pulseUs > 2000 || frequency != 50)
    throw Error("MOTION_RANGE", "Servo pulses must remain in 1000..2000 us at 50 Hz");
  return unsigned(std::lround(double(pulseUs) * frequency * 4096 / 1000000));
}
class MockServo final : public ServoBus {
public:
  void initialize() override {}
  void pulses(int l, int r) override { (void)pwmTicks(l); (void)pwmTicks(r); }
  void enable() override {}
  void disable() override {}
};
std::unique_ptr<ServoBus> makeServo(const Config& c) {
  if (c.mock) return std::make_unique<MockServo>();
#ifdef DESKPET_LINUX
  return makeLinuxServo(c);
#else
  throw Error("UNSUPPORTED_PLATFORM", "Linux servo backend was not built");
#endif
}
}
