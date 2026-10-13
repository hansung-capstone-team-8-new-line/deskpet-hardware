#include "deskpet/pca9685.hpp"
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
namespace deskpet {
class LinuxServo final : public ServoBus {
  Config config_; int fd_ = -1;
  Pca9685 pwm_;
public:
  explicit LinuxServo(Config c) : config_(c), pwm_(c, [this](const Bytes& bytes) {
    if (::write(fd_, bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size())) throw Error("SERVO_ERROR", "I2C register write failed");
  }) {}
  ~LinuxServo() override {
    if (fd_ >= 0) { try { pwm_.disable(); } catch (...) {} ::close(fd_); }
  }
  void initialize() override {
    if (config_.i2cDevice.empty()) throw Error("SERVO_ERROR", "Choose the header I2C adapter");
    fd_ = ::open(config_.i2cDevice.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0 || ioctl(fd_, I2C_SLAVE, config_.address) < 0)
      throw Error("SERVO_ERROR", "Cannot open configured PCA9685 I2C address");
    pwm_.initialize();
  }
  void pulses(int l, int r) override { pwm_.pulses(l, r); }
  void enable() override { pwm_.enable(); }
  void disable() override { pwm_.disable(); }
};
std::unique_ptr<ServoBus> makeLinuxServo(const Config& c) { return std::make_unique<LinuxServo>(c); }
}
