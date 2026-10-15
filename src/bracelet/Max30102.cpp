#include "Max30102.h"

#include <Arduino.h>

enum : uint8_t {
  REG_FIFO_WR = 0x04, REG_FIFO_OVF = 0x05, REG_FIFO_RD = 0x06, REG_FIFO_DATA = 0x07,
  REG_FIFO_CONFIG = 0x08, REG_MODE = 0x09, REG_SPO2 = 0x0A, REG_LED_RED = 0x0C, REG_LED_IR = 0x0D,
  REG_TEMP_INT = 0x1F, REG_TEMP_FRAC = 0x20, REG_TEMP_CONFIG = 0x21, REG_PART_ID = 0xFF,
};

bool Max30102::begin(TwoWire &wire, uint8_t addr) {
  wire_ = &wire;
  addr_ = addr;
  uint8_t id = 0;
  if (!read(REG_PART_ID, &id, 1) || id != 0x15) return false;

  write(REG_MODE, 0x40);  // сброс
  for (int i = 0; i < 100; i++) {
    uint8_t mode = 0x40;
    read(REG_MODE, &mode, 1);
    if (!(mode & 0x40)) break;
    delay(1);
  }
  write(REG_FIFO_WR, 0);
  write(REG_FIFO_OVF, 0);
  write(REG_FIFO_RD, 0);
  write(REG_FIFO_CONFIG, (2 << 5) | (1 << 4) | 0x0F);  // усреднение 4, перезапись по кругу
  write(REG_SPO2, (3 << 5) | (3 << 2) | 3);           // АЦП 16384 нА, 400 Гц, импульс 411 мкс (18 бит)
  setLedCurrent(0x18, 0x40);
  return write(REG_MODE, 0x03);  // режим SpO2: красный + ИК
}

void Max30102::setLedCurrent(uint8_t red, uint8_t ir) {
  red_ = red;
  ir_ = ir;
  write(REG_LED_RED, red);
  write(REG_LED_IR, ir);
}

int Max30102::readFifo(uint32_t *red, uint32_t *ir, int max) {
  uint8_t ptr[3];  // WR, OVF, RD — подряд
  if (!read(REG_FIFO_WR, ptr, 3)) return 0;
  int n = ptr[1] ? 32 : (ptr[0] - ptr[2]) & 0x1F;  // переполнение — значит, FIFO полон
  if (n > max) n = max;
  int done = 0;
  while (done < n) {
    int chunk = n - done > 8 ? 8 : n - done;
    uint8_t buf[8 * 6];
    if (!read(REG_FIFO_DATA, buf, chunk * 6)) break;
    for (int i = 0; i < chunk; i++) {
      const uint8_t *b = buf + i * 6;
      red[done + i] = ((uint32_t)b[0] << 16 | (uint32_t)b[1] << 8 | b[2]) & 0x3FFFF;
      ir[done + i] = ((uint32_t)b[3] << 16 | (uint32_t)b[4] << 8 | b[5]) & 0x3FFFF;
    }
    done += chunk;
  }
  return done;
}

void Max30102::startTemperature() { write(REG_TEMP_CONFIG, 0x01); }

bool Max30102::readTemperature(float &celsius) {
  uint8_t cfg = 1;
  if (!read(REG_TEMP_CONFIG, &cfg, 1) || (cfg & 0x01)) return false;  // бит сбрасывается по готовности
  uint8_t t[2];
  if (!read(REG_TEMP_INT, t, 2)) return false;
  celsius = (int8_t)t[0] + (t[1] & 0x0F) * 0.0625f;
  return true;
}

bool Max30102::write(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  wire_->write(value);
  return wire_->endTransmission() == 0;
}

bool Max30102::read(uint8_t reg, uint8_t *buf, size_t len) {
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) return false;
  if (wire_->requestFrom(addr_, len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = wire_->read();
  return true;
}
