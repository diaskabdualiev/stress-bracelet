// Тестовая прошивка Waveshare RP2040-Zero.
//
//   I2C:  OLED SSD1306 0.91" (128x32), MPU6050 (GY-521), MAX30102 (GY-MAX30102)
//   GP28: вибромотор (PWM)
//   GP26: адресные светодиоды WS2812
//
// Что делает:
//   * ищет шину I2C и все три модуля, пишет результат на OLED и в Serial;
//   * самотест при старте: светодиоды R/G/B/W, мотор — 3 импульса и плавный разгон;
//   * OLED листает страницы: статус -> MPU6050 -> MAX30102 (каждые 4 с или по встряхиванию);
//   * наклон платы меняет цвет светодиодов;
//   * палец на MAX30102: экран пульса с графиком, светодиоды мигают красным, мотор тикает в такт;
//   * встряхивание — вибро + следующая страница;
//   * Serial 115200: команда h — список команд.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <MAX30105.h>
#include <heartRate.h>
#include <hardware/adc.h>
#include <hardware/gpio.h>

#include "config.h"

static_assert(I2C_SDA_PIN % 2 == 0 && I2C_SCL_PIN % 2 == 1 &&
                  ((I2C_SDA_PIN >> 1) & 1) == ((I2C_SCL_PIN >> 1) & 1),
              "SDA/SCL должны принадлежать одному контроллеру I2C (например GP4/GP5 или GP2/GP3)");

// ============================================================== I2C

static TwoWire *bus = &Wire;
static uint8_t i2cSda = I2C_SDA_PIN, i2cScl = I2C_SCL_PIN;
static uint8_t oledAddr = 0, mpuAddr = 0;  // 0 — не найден
static bool maxFound = false;

// У RP2040 контроллер задан пином: GP0/1, GP4/5, GP8/9, GP12/13 -> I2C0 (Wire),
// GP2/3, GP6/7, GP10/11, GP14/15 -> I2C1 (Wire1).
static TwoWire &busForPin(uint8_t sda) { return ((sda >> 1) & 1) ? Wire1 : Wire; }

static void i2cOpen(uint8_t sda, uint8_t scl) {
  bus = &busForPin(sda);
  bus->setSDA(sda);
  bus->setSCL(scl);
  bus->begin();
  bus->setClock(I2C_FREQ);
  i2cSda = sda;
  i2cScl = scl;
}

static bool i2cPresent(uint8_t addr) {
  bus->beginTransmission(addr);
  return bus->endTransmission() == 0;
}

static void i2cFindDevices() {
  oledAddr = i2cPresent(0x3C) ? 0x3C : i2cPresent(0x3D) ? 0x3D : 0;
  mpuAddr = i2cPresent(0x68) ? 0x68 : i2cPresent(0x69) ? 0x69 : 0;
  maxFound = i2cPresent(MAX30105_ADDRESS);
}

static int i2cDeviceCount() { return (oledAddr != 0) + (mpuAddr != 0) + maxFound; }

// Перебирает пары пинов RP2040-Zero (кроме GP16 — встроенный светодиод — и GP26..29),
// пропуская пины ленты и мотора. Останавливается на первой паре, где нашлись все три модуля,
// иначе берёт пару с максимумом.
static void i2cSetup() {
  static const uint8_t pairs[][2] = {
      {I2C_SDA_PIN, I2C_SCL_PIN},
      {4, 5}, {0, 1}, {2, 3}, {6, 7}, {8, 9}, {10, 11}, {12, 13}, {14, 15},
  };
  const int n = I2C_AUTODETECT ? sizeof(pairs) / sizeof(pairs[0]) : 1;
  int best = 0, bestCount = -1;
  for (int i = 0; i < n; i++) {
    if (i > 0 && pairs[i][0] == pairs[0][0] && pairs[i][1] == pairs[0][1]) continue;
    bool busy = false;
    for (uint8_t pin : pairs[i]) busy |= pin == PIN_LEDS || pin == PIN_VIBRO;
    if (busy) continue;
    i2cOpen(pairs[i][0], pairs[i][1]);
    i2cFindDevices();
    int count = i2cDeviceCount();
    bus->end();
    Serial.printf("I2C SDA GP%u / SCL GP%u: модулей %d\n", pairs[i][0], pairs[i][1], count);
    if (count > bestCount) {
      best = i;
      bestCount = count;
    }
    if (count == 3) break;
  }
  i2cOpen(pairs[best][0], pairs[best][1]);
  i2cFindDevices();
}

static void i2cScan() {
  Serial.printf("Скан I2C (Wire%d, SDA GP%u, SCL GP%u):", bus == &Wire1, i2cSda, i2cScl);
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    if (i2cPresent(a)) {
      Serial.printf(" 0x%02X", a);
      found++;
    }
  }
  Serial.println(found ? "" : " ничего");
}

// ============================================================== OLED

static Adafruit_SSD1306 *oled = nullptr;

static bool oledInit() {
  if (!oledAddr) return false;
  oled = new Adafruit_SSD1306(OLED_WIDTH, OLED_HEIGHT, bus, -1, I2C_FREQ, I2C_FREQ);
  if (!oled->begin(SSD1306_SWITCHCAPVCC, oledAddr, true, false)) {
    delete oled;
    oled = nullptr;
    return false;
  }
  oled->setTextColor(SSD1306_WHITE);
  oled->setTextWrap(false);
  return true;
}

static void oledShow(const char *l1, const char *l2 = "", const char *l3 = "", const char *l4 = "") {
  if (!oled) return;
  oled->clearDisplay();
  const char *lines[] = {l1, l2, l3, l4};
  for (int i = 0; i < 4; i++) {
    oled->setCursor(0, i * 8);
    oled->print(lines[i]);
  }
  oled->display();
}

// ============================================================== MPU6050 (регистры напрямую — работает и с клонами)

struct Imu {
  float ax, ay, az;  // g
  float gx, gy, gz;  // °/с
  float temp, roll, pitch, mag;
  uint8_t whoami;
  bool ok;  // последнее чтение успешно
} imu;
static bool mpuOk = false;

static bool mpuWrite(uint8_t reg, uint8_t val) {
  bus->beginTransmission(mpuAddr);
  bus->write(reg);
  bus->write(val);
  return bus->endTransmission() == 0;
}

static bool mpuRead(uint8_t reg, uint8_t *dst, uint8_t len) {
  bus->beginTransmission(mpuAddr);
  bus->write(reg);
  if (bus->endTransmission(false) != 0) return false;
  if (bus->requestFrom(mpuAddr, (size_t)len) != len) return false;
  for (uint8_t i = 0; i < len; i++) dst[i] = bus->read();
  return true;
}

static bool mpuInit() {
  if (!mpuAddr) return false;
  mpuRead(0x75, &imu.whoami, 1);  // WHO_AM_I: 0x68 — MPU6050, 0x70 — MPU6500, 0x98/0x72 — клоны
  mpuWrite(0x6B, 0x80);           // сброс
  delay(100);
  return mpuWrite(0x6B, 0x01)     // проснуться, тактирование от гироскопа
         && mpuWrite(0x1A, 0x03)  // DLPF ~44 Гц
         && mpuWrite(0x1B, 0x00)  // гироскоп ±250 °/с
         && mpuWrite(0x1C, 0x00); // акселерометр ±2g
}

static void mpuUpdate() {
  uint8_t b[14];
  imu.ok = mpuRead(0x3B, b, sizeof b);
  if (!imu.ok) return;
  auto s16 = [&](int i) { return (int16_t)(b[i] << 8 | b[i + 1]); };
  imu.ax = s16(0) / 16384.0f;
  imu.ay = s16(2) / 16384.0f;
  imu.az = s16(4) / 16384.0f;
  imu.temp = imu.whoami == 0x68 ? s16(6) / 340.0f + 36.53f : s16(6) / 333.87f + 21.0f;
  imu.gx = s16(8) / 131.0f;
  imu.gy = s16(10) / 131.0f;
  imu.gz = s16(12) / 131.0f;
  imu.roll = atan2f(imu.ay, imu.az) * RAD_TO_DEG;
  imu.pitch = atan2f(-imu.ax, sqrtf(imu.ay * imu.ay + imu.az * imu.az)) * RAD_TO_DEG;
  imu.mag = sqrtf(imu.ax * imu.ax + imu.ay * imu.ay + imu.az * imu.az);
}

// ============================================================== Вибромотор

static uint32_t vibroUntil = 0;
static bool vibroOn = false;

static void vibro(uint16_t ms, uint8_t pwm) {
  analogWrite(PIN_VIBRO, pwm);
  vibroUntil = millis() + ms;
  vibroOn = true;
}

static void vibroUpdate() {
  if (vibroOn && (int32_t)(millis() - vibroUntil) >= 0) {
    analogWrite(PIN_VIBRO, 0);
    vibroOn = false;
  }
}

static void vibroTest() {
  for (int i = 0; i < 3; i++) {
    analogWrite(PIN_VIBRO, 255);
    delay(150);
    analogWrite(PIN_VIBRO, 0);
    delay(150);
  }
  for (int p = 80; p <= 255; p += 5) {  // плавный разгон
    analogWrite(PIN_VIBRO, p);
    delay(20);
  }
  delay(300);
  analogWrite(PIN_VIBRO, 0);
  vibroOn = false;
}

// Диагностика пина мотора: АЦП меряет реальное напряжение на пине (GP26..29 — входы ADC0..3)
// в разных режимах. По цифрам видно, что к пину подключено.
#if PIN_VIBRO >= 26 && PIN_VIBRO <= 29
static float vibroPinVolts() {
  uint32_t sum = 0;
  for (int i = 0; i < 32; i++) sum += adc_read();
  return sum * 3.3f / (32 * 4096.0f);
}

// СКО модуля ускорения за ms миллисекунд, g. Если мотор и MPU на одной плате — видно, крутится ли мотор.
static float accelVibration(uint32_t ms) {
  uint8_t b[6];
  double s = 0, s2 = 0;
  uint32_t n = 0;
  for (uint32_t t0 = millis(); millis() - t0 < ms;) {
    if (!mpuRead(0x3B, b, sizeof b)) continue;
    float ax = (int16_t)(b[0] << 8 | b[1]) / 16384.0f;
    float ay = (int16_t)(b[2] << 8 | b[3]) / 16384.0f;
    float az = (int16_t)(b[4] << 8 | b[5]) / 16384.0f;
    float m = sqrtf(ax * ax + ay * ay + az * az);
    s += m;
    s2 += m * m;
    n++;
    delayMicroseconds(500);
  }
  if (!n) return NAN;
  double mean = s / n;
  return sqrt(max(0.0, s2 / n - mean * mean));
}

static void vibroDiag() {
  adc_init();
  adc_select_input(PIN_VIBRO - 26);
  vibroOn = false;

  pinMode(PIN_VIBRO, INPUT_PULLDOWN);
  delay(20);
  float vPullDown = vibroPinVolts();
  pinMode(PIN_VIBRO, INPUT_PULLUP);
  delay(20);
  float vPullUp = vibroPinVolts();
  pinMode(PIN_VIBRO, OUTPUT);
  digitalWrite(PIN_VIBRO, LOW);
  delay(20);
  float vLow = vibroPinVolts();
  float vibOff = mpuOk ? accelVibration(500) : NAN;
  digitalWrite(PIN_VIBRO, HIGH);
  delay(200);
  float vHigh4 = vibroPinVolts();
  gpio_set_drive_strength(PIN_VIBRO, GPIO_DRIVE_STRENGTH_12MA);
  delay(100);
  float vHigh12 = vibroPinVolts();
  float vibOn = mpuOk ? accelVibration(500) : NAN;
  digitalWrite(PIN_VIBRO, LOW);
  gpio_set_drive_strength(PIN_VIBRO, GPIO_DRIVE_STRENGTH_4MA);

  Serial.printf("Диагностика GP%d (напряжение на пине):\n", PIN_VIBRO);
  Serial.printf("  вход, подтяжка к GND : %.2f В\n", vPullDown);
  Serial.printf("  вход, подтяжка к 3V3 : %.2f В\n", vPullUp);
  Serial.printf("  выход LOW            : %.2f В\n", vLow);
  Serial.printf("  выход HIGH, 4 мА     : %.2f В\n", vHigh4);
  Serial.printf("  выход HIGH, 12 мА    : %.2f В\n", vHigh12);
  if (mpuOk) Serial.printf("Вибрация по MPU (СКО |a|): мотор выкл %.4f g, вкл %.4f g\n", vibOff, vibOn);
}
#else
static void vibroDiag() { Serial.println("Диагностика мотора работает только на GP26..GP29 (АЦП)"); }
#endif

// ============================================================== Адресные светодиоды

static Adafruit_NeoPixel strip(LED_COUNT, PIN_LEDS, NEO_GRB + NEO_KHZ800);
static uint8_t beatGlow = 0;

static void ledTest() {
  const uint32_t colors[] = {strip.Color(255, 0, 0), strip.Color(0, 255, 0),
                             strip.Color(0, 0, 255), strip.Color(255, 255, 255)};
  for (uint32_t c : colors) {
    strip.fill(c);
    strip.show();
    delay(400);
  }
  strip.clear();
  strip.show();
}

// ============================================================== MAX30102

static MAX30105 pox;
static bool maxOk = false;
static const int MAX_SPS = 100;  // 400 Гц / усреднение 4
static const int RATE_SIZE = 4;

struct Pulse {
  uint32_t ir, red;
  bool finger;
  int bpm, bpmAvg;
  float temp;
} pulse;

static uint32_t sampleNo = 0, lastBeatSample = 0, lastBeatMs = 0;
static uint8_t rates[RATE_SIZE], rateSpot = 0, rateCount = 0;

// График пульса: 1 точка = 4 отсчёта (25 точек/с), 128 точек ≈ 5 с
static int16_t wave[OLED_WIDTH];
static uint8_t waveHead = 0, waveN = 0;
static float irDc = 0, waveAcc = 0;

static bool maxInit() {
  if (!maxFound) return false;
  if (!pox.begin(*bus, I2C_FREQ)) return false;
  // ток светодиодов 0x1F (~6 мА), усреднение 4, Red+IR, 400 Гц, импульс 411 мкс, АЦП 4096 нА
  pox.setup(0x1F, 4, 2, 400, 411, 4096);
  pox.enableDIETEMPRDY();  // нужно для readTemperature()
  pulse.temp = pox.readTemperature();
  return true;
}

static void onBeat() {
  lastBeatMs = millis();
  beatGlow = 255;
  if (VIBRO_BEAT_MS) vibro(VIBRO_BEAT_MS, VIBRO_BEAT_PWM);
}

static void processSample(uint32_t ir, uint32_t red) {
  sampleNo++;
  pulse.ir = ir;
  pulse.red = red;

  bool finger = ir > FINGER_IR_MIN;
  if (finger != pulse.finger) {  // палец положили/убрали — всё сначала
    pulse.finger = finger;
    pulse.bpm = pulse.bpmAvg = 0;
    rateSpot = rateCount = 0;
    irDc = ir;
    memset(wave, 0, sizeof wave);
  }
  if (!finger) return;

  irDc += (ir - irDc) * 0.02f;  // убираем постоянную составляющую
  waveAcc += irDc - ir;         // инверсия: пик систолы смотрит вверх
  if (++waveN == 4) {
    wave[waveHead] = (int16_t)constrain(waveAcc / 4, -32000.0f, 32000.0f);
    waveHead = (waveHead + 1) % OLED_WIDTH;
    waveAcc = 0;
    waveN = 0;
  }

  if (checkForBeat(ir)) {
    uint32_t dt = sampleNo - lastBeatSample;  // считаем по отсчётам, а не по millis()
    lastBeatSample = sampleNo;
    int bpm = 60 * MAX_SPS / (int)dt;
    if (bpm > 30 && bpm < 220) {
      pulse.bpm = bpm;
      rates[rateSpot] = bpm;
      rateSpot = (rateSpot + 1) % RATE_SIZE;
      if (rateCount < RATE_SIZE) rateCount++;
      int sum = 0;
      for (int i = 0; i < rateCount; i++) sum += rates[i];
      pulse.bpmAvg = sum / rateCount;
    }
    onBeat();
  }
}

// Внутренний буфер библиотеки — всего 4 отсчёта, поэтому вызывать часто (не реже ~30 мс).
static void maxUpdate() {
  pox.check();
  while (pox.available()) {
    processSample(pox.getFIFOIR(), pox.getFIFORed());
    pox.nextSample();
  }
}

// ============================================================== Страницы OLED

enum Page : uint8_t { PAGE_STATUS, PAGE_IMU, PAGE_PULSE, PAGE_COUNT };
static uint8_t page = PAGE_STATUS;
static uint32_t pageSince = 0;

static void nextPage() {
  page = (page + 1) % PAGE_COUNT;
  pageSince = millis();
}

static void pageUpdate(uint32_t now) {
  if (pulse.finger) {  // палец на датчике — держим экран пульса
    page = PAGE_PULSE;
    pageSince = now;
  } else if (now - pageSince >= PAGE_INTERVAL_MS) {
    nextPage();
  }
}

static void printAddr(uint8_t addr) {
  if (addr) oled->printf("%02X", addr);
  else oled->print("--");
}

static void drawStatus() {
  oled->setCursor(0, 0);
  oled->printf("RP2040 TEST %8lus", (unsigned long)(millis() / 1000));
  oled->setCursor(0, 8);
  oled->printf("I2C SDA%u SCL%u W%d", i2cSda, i2cScl, bus == &Wire1);
  oled->setCursor(0, 16);
  oled->print("OLED:");
  printAddr(oledAddr);
  oled->print(" MPU:");
  printAddr(mpuOk ? mpuAddr : 0);
  oled->print(" MAX:");
  printAddr(maxOk ? MAX30105_ADDRESS : 0);
  oled->setCursor(0, 24);
  oled->printf("LED GP%d x%d VIB GP%d", PIN_LEDS, LED_COUNT, PIN_VIBRO);
}

static void drawImu() {
  oled->setCursor(0, 0);
  if (!mpuOk) {
    oled->print("MPU6050: not found");
    oled->setCursor(0, 8);
    oled->print("addr 0x68 / 0x69");
    oled->setCursor(0, 16);
    oled->print("check VCC GND SDA SCL");
    return;
  }
  if (!imu.ok) {
    oled->printf("MPU id%02X  READ ERROR", imu.whoami);
    return;
  }
  oled->printf("MPU id%02X  T %5.1fC", imu.whoami, imu.temp);
  oled->setCursor(0, 8);
  oled->printf("A%+5.2f%+6.2f%+6.2f", imu.ax, imu.ay, imu.az);
  oled->setCursor(0, 16);
  oled->printf("G%+6.1f%+6.1f%+6.1f", imu.gx, imu.gy, imu.gz);
  oled->setCursor(0, 24);
  oled->printf("R%+6.1f P%+6.1f %4.2fg", imu.roll, imu.pitch, imu.mag);
}

static void drawWave(int y0, int h) {
  int16_t lo = INT16_MAX, hi = INT16_MIN;
  for (int16_t v : wave) {
    lo = min(lo, v);
    hi = max(hi, v);
  }
  int32_t range = max<int32_t>(hi - lo, 40);  // чтобы не раздувать шум
  int32_t mid = ((int32_t)hi + lo) / 2;
  int prevY = 0;
  for (int x = 0; x < OLED_WIDTH; x++) {
    int32_t v = wave[(waveHead + x) % OLED_WIDTH];
    int y = y0 + h - 1 - (int)((v - mid + range / 2) * (h - 1) / range);
    if (x) oled->drawLine(x - 1, prevY, x, y, SSD1306_WHITE);
    prevY = y;
  }
}

static void drawPulse() {
  oled->setCursor(0, 0);
  if (!maxOk) {
    oled->print("MAX30102: not found");
    oled->setCursor(0, 8);
    oled->print("addr 0x57");
    oled->setCursor(0, 16);
    oled->print("check VCC GND SDA SCL");
    return;
  }
  if (!pulse.finger) {
    oled->printf("MAX30102  T %5.1fC", pulse.temp);
    oled->setCursor(0, 8);
    oled->printf("IR  %lu", (unsigned long)pulse.ir);
    oled->setCursor(0, 16);
    oled->printf("RED %lu", (unsigned long)pulse.red);
    oled->setCursor(0, 24);
    oled->print("Put finger on sensor");
    return;
  }
  if (pulse.bpmAvg) oled->printf("BPM %3d  avg %3d", pulse.bpm, pulse.bpmAvg);
  else oled->print("BPM ...  hold still");
  if (millis() - lastBeatMs < 150) oled->fillCircle(123, 3, 3, SSD1306_WHITE);
  else oled->drawCircle(123, 3, 3, SSD1306_WHITE);
  drawWave(10, OLED_HEIGHT - 10);
}

static void drawPage() {
  oled->clearDisplay();
  switch (page) {
    case PAGE_STATUS: drawStatus(); break;
    case PAGE_IMU: drawImu(); break;
    case PAGE_PULSE: drawPulse(); break;
  }
  oled->display();
}

// ============================================================== Логика теста

static uint32_t lastShakeMs = 0;
static bool streamOn = true;

static void checkShake() {
  if (!imu.ok || fabsf(imu.mag - 1.0f) < SHAKE_G || millis() - lastShakeMs < 700) return;
  lastShakeMs = millis();
  vibro(150, 255);
  nextPage();
  Serial.println("Встряхивание!");
}

static void ledUpdate() {
  if (pulse.finger) {  // красный, вспыхивает на каждый удар
    uint8_t level = max<uint8_t>(beatGlow, 12);
    strip.fill(strip.Color(level, 0, 0));
    beatGlow = beatGlow > 15 ? beatGlow - 15 : 0;
  } else {  // радуга: угол наклона задаёт оттенок; без MPU — просто крутится
    uint16_t hue = imu.ok ? (uint16_t)((imu.roll + 180.0f) * (65535.0f / 360.0f)) : (uint16_t)(millis() * 8);
    for (int i = 0; i < LED_COUNT; i++)
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV((uint16_t)(hue + i * 65536L / LED_COUNT))));
  }
  strip.show();
}

static void printReport() {
  Serial.printf("I2C: Wire%d, SDA GP%u, SCL GP%u\n", bus == &Wire1, i2cSda, i2cScl);
  Serial.printf("  OLED SSD1306: %s", oled ? "OK" : "НЕ НАЙДЕН");
  if (oled) Serial.printf(" (0x%02X)", oledAddr);
  Serial.printf("\n  MPU6050:      %s", mpuOk ? "OK" : "НЕ НАЙДЕН");
  if (mpuOk) Serial.printf(" (0x%02X, WHO_AM_I=0x%02X)", mpuAddr, imu.whoami);
  Serial.printf("\n  MAX30102:     %s\n", maxOk ? "OK (0x57)" : "НЕ НАЙДЕН");
  Serial.printf("  Светодиоды:   GP%d, %d шт.\n  Вибромотор:   GP%d\n", PIN_LEDS, LED_COUNT, PIN_VIBRO);
}

static void printHelp() {
  Serial.println("Команды: h — помощь, s — скан I2C, v — тест мотора, m — диагностика пина мотора,");
  Serial.println("         l — тест светодиодов,");
  Serial.println("         n — следующая страница OLED, d — вкл/выкл поток данных, r — отчёт");
}

static void printData() {
  if (mpuOk && imu.ok)
    Serial.printf("IMU a=%+.2f,%+.2f,%+.2f g=%+.1f,%+.1f,%+.1f T=%.1f roll=%+.0f pitch=%+.0f  ",
                  imu.ax, imu.ay, imu.az, imu.gx, imu.gy, imu.gz, imu.temp, imu.roll, imu.pitch);
  if (maxOk)
    Serial.printf("MAX ir=%lu red=%lu finger=%d bpm=%d avg=%d T=%.1f", (unsigned long)pulse.ir,
                  (unsigned long)pulse.red, pulse.finger, pulse.bpm, pulse.bpmAvg, pulse.temp);
  if (mpuOk || maxOk) Serial.println();
}

static void handleSerial() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'h': case '?': printHelp(); break;
      case 'r': printReport(); break;
      case 's': i2cScan(); break;
      case 'v': Serial.println("Тест вибромотора"); vibroTest(); break;
      case 'm': vibroDiag(); break;
      case 'l': Serial.println("Тест светодиодов"); ledTest(); break;
      case 'n': nextPage(); break;
      case 'd': streamOn = !streamOn; break;
    }
  }
}

void setup() {
  pinMode(PIN_VIBRO, OUTPUT);
  digitalWrite(PIN_VIBRO, LOW);
  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  strip.clear();
  strip.show();

  Serial.begin(115200);
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 2000;) delay(10);
  Serial.println("\n=== RP2040-Zero: тест железа ===");

  i2cSetup();
  oledInit();
  oledShow("RP2040-Zero TEST", "init sensors...");
  mpuOk = mpuInit();
  maxOk = maxInit();
  printReport();

  char buf[22];
  snprintf(buf, sizeof buf, "LED GP%d: R G B W", PIN_LEDS);
  oledShow("SELF-TEST", buf);
  ledTest();
  snprintf(buf, sizeof buf, "VIBRO GP%d", PIN_VIBRO);
  oledShow("SELF-TEST", buf);
  vibroTest();

  printHelp();
  pageSince = millis();
}

void loop() {
  static uint32_t tImu = 0, tLed = 0, tOled = 0, tTemp = 0, tPrint = 0;
  uint32_t now = millis();

  if (maxOk) maxUpdate();
  if (mpuOk && now - tImu >= 20) {
    tImu = now;
    mpuUpdate();
    checkShake();
  }
  if (now - tLed >= 20) {
    tLed = now;
    ledUpdate();
  }
  vibroUpdate();
  if (oled && now - tOled >= 100) {
    tOled = now;
    pageUpdate(now);
    drawPage();
  }
  // Чтение температуры блокирует ~30 мс — только когда пульс не меряем
  if (maxOk && !pulse.finger && now - tTemp >= 5000) {
    tTemp = now;
    pulse.temp = pox.readTemperature();
  }
  if (streamOn && now - tPrint >= 500) {
    tPrint = now;
    printData();
  }
  handleSerial();
}
