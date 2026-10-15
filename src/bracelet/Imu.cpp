#include "Imu.h"

#include <Arduino.h>

bool Imu::begin(TwoWire &wire) {
  wire_ = &wire;
  wire.beginTransmission(0x68);
  if (wire.endTransmission() == 0) {
    addr_ = 0x68;
  } else {
    wire.beginTransmission(0x69);
    if (wire.endTransmission() != 0) return false;
    addr_ = 0x69;
  }
  wire.beginTransmission(addr_);
  wire.write(0x75);  // WHO_AM_I: 0x68 — MPU6050, 0x70 — MPU6500
  if (wire.endTransmission(false) == 0 && wire.requestFrom(addr_, (size_t)1) == 1) whoami_ = wire.read();

  write(0x6B, 0x80);  // сброс
  delay(100);
  bool ok = write(0x6B, 0x01)    // проснуться, тактирование от гироскопа
            && write(0x1A, 0x03)  // фильтр гироскопа ~41 Гц
            && write(0x1B, 0x08)  // гироскоп ±500 °/с
            && write(0x1C, 0x08); // акселерометр ±4g: прыжки на запястье легко дают больше 2g
  if (whoami_ != 0x68) write(0x1D, 0x03);  // MPU6500: отдельный фильтр акселерометра ~41 Гц
  return ok;
}

bool Imu::read(float &ax, float &ay, float &az, float &gx, float &gy, float &gz) {
  uint8_t b[14];
  wire_->beginTransmission(addr_);
  wire_->write(0x3B);
  if (wire_->endTransmission(false) != 0 || wire_->requestFrom(addr_, sizeof b) != sizeof b) return false;
  for (uint8_t &v : b) v = wire_->read();
  auto s16 = [&](int i) { return (int16_t)(b[i] << 8 | b[i + 1]); };
  ax = s16(0) / 8192.0f;
  ay = s16(2) / 8192.0f;
  az = s16(4) / 8192.0f;
  gx = s16(8) / 65.5f;
  gy = s16(10) / 65.5f;
  gz = s16(12) / 65.5f;
  return true;
}

bool Imu::write(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  wire_->write(value);
  return wire_->endTransmission() == 0;
}
