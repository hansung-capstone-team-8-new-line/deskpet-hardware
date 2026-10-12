#include "deskpet/backends.hpp"
#include <gpiod.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <thread>

namespace deskpet {
class LinuxServo final : public ServoBus {
  Config config_; int fd_ = -1; gpiod_chip* chip_ = nullptr; gpiod_line* oe_ = nullptr;
  bool requested_ = false;
  void writeReg(uint8_t reg, uint8_t value) {
    uint8_t b[]{reg, value};
    if (::write(fd_, b, 2) != 2) throw Error("SERVO_ERROR", "I2C register write failed: " + std::string(std::strerror(errno)));
  }
  void channel(int ch, unsigned tick) {
    const uint8_t b[]{uint8_t(0x06 + 4 * ch), 0, 0, uint8_t(tick & 255), uint8_t((tick >> 8) & 15)};
    if (::write(fd_, b, 5) != 5) throw Error("SERVO_ERROR", "I2C pulse write failed");
  }
public:
  explicit LinuxServo(Config c) : config_(std::move(c)) {}
  ~LinuxServo() override {
    if (requested_) { gpiod_line_set_value(oe_, 1); gpiod_line_release(oe_); }
    if (chip_) gpiod_chip_close(chip_);
    if (fd_ >= 0) ::close(fd_);
  }
  void initialize() override {
    // Request OE HIGH atomically, BEFORE accessing PWM registers. The external
    // non-inverting buffer and pull-ups must also hold OE high during boot.
    chip_ = gpiod_chip_open_by_label(config_.gpioChipLabel.c_str());
    if (!chip_) throw Error("SERVO_ERROR", "GPIO chip not found; check the board and gpioChipLabel");
    oe_ = gpiod_chip_find_line(chip_, config_.gpioLine.c_str());
    if (!oe_ || gpiod_line_request_output(oe_, "deskpet-oe", 1) < 0)
      throw Error("SERVO_ERROR", "Cannot acquire OE GPIO; verify pinmux, permissions and gpioLine");
    requested_ = true;
    if (config_.i2cDevice.empty()) throw Error("SERVO_ERROR", "Choose the header I2C adapter in configuration");
    fd_ = ::open(config_.i2cDevice.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0 || ioctl(fd_, I2C_SLAVE, config_.address) < 0) throw Error("SERVO_ERROR", "Cannot open configured PCA9685 I2C address");
    writeReg(0x00, 0x30); // SLEEP + auto-increment, ALLCALL disabled
    writeReg(0xfe, 121);  // round(25 MHz / (4096 * 50 Hz)) - 1
    writeReg(0x01, 0x04); // Totem pole; outputs LOW when OE is HIGH (OUTNE=00)
    writeReg(0xfa, 0); writeReg(0xfb, 0); writeReg(0xfc, 0); writeReg(0xfd, 0);
    // Disable each channel individually. Do not leave the global FULL_OFF bit set.
    for (int ch = 0; ch < 16; ++ch) writeReg(uint8_t(0x09 + 4 * ch), 0x10);
    writeReg(0x00, 0x20);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    writeReg(0x00, 0xa0); // RESTART + auto-increment
  }
  void pulses(int l, int r) override {
    channel(config_.left.channel, pwmTicks(l)); channel(config_.right.channel, pwmTicks(r));
  }
  void enable() override {
    if (!requested_ || gpiod_line_set_value(oe_, 0) < 0) throw Error("SERVO_ERROR", "Cannot enable OE");
  }
  void disable() override {
    if (!requested_ || gpiod_line_set_value(oe_, 1) < 0) throw Error("SERVO_STOP_UNCONFIRMED", "Cannot confirm OE is disabled");
  }
};
std::unique_ptr<ServoBus> makeLinuxServo(const Config& c) { return std::make_unique<LinuxServo>(c); }
}
