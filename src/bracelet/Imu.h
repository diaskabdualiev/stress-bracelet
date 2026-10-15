#pragma once
#include <Wire.h>

// MPU6050 / MPU6500 (на GY-521 часто стоит клон MPU6500) — регистры напрямую.
class Imu {
 public:
  bool begin(TwoWire &wire);
  bool read(float &ax, float &ay, float &az, float &gx, float &gy, float &gz);  // g, °/с
  uint8_t whoAmI() const { return whoami_; }

 private:
  bool write(uint8_t reg, uint8_t value);
  TwoWire *wire_ = nullptr;
  uint8_t addr_ = 0x68, whoami_ = 0;
};
