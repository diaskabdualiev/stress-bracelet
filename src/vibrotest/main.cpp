// Тест вибромотора (вибромодуль, вход IN на GP28).
//
// Мотор по кругу: 1 с на полную → пауза → 3 коротких → пауза → плавный разгон → пауза.
// Кнопка BOOT — мотор включён постоянно (удобно мерить мультиметром: VCC–GND модуля
// и напряжение на выводах мотора), ещё раз — обратно по кругу.
// На экране, в Serial (115200) и светодиодом — что делает мотор; на экране и в Serial —
// напряжение на пине (GP28 — вход АЦП).
//
//   pio run -e vibrotest -t upload    — залить этот тест
//   pio run -t upload                 — вернуть прошивку браслета

#include <Adafruit_NeoPixel.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>
#include <hardware/adc.h>

#include "config.h"

static_assert(PIN_VIBRO >= 26 && PIN_VIBRO <= 29, "замер напряжения возможен только на GP26..GP29 (АЦП)");

static TwoWire &bus = ((I2C_SDA_PIN >> 1) & 1) ? Wire1 : Wire;
static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &bus, -1, I2C_FREQ, I2C_FREQ);
static Adafruit_NeoPixel led(LED_COUNT, PIN_LEDS, NEO_GRB + NEO_KHZ800);
static bool oledOk = false;

struct Step {
  const char *name;
  uint16_t ms;
  uint8_t from, to;  // мощность 0..255, плавно от from к to
};
static const Step steps[] = {
    {"FULL", 1000, 255, 255}, {"OFF", 1000, 0, 0},       {"PULSE", 150, 255, 255}, {"OFF", 150, 0, 0},
    {"PULSE", 150, 255, 255}, {"OFF", 150, 0, 0},        {"PULSE", 150, 255, 255}, {"OFF", 1000, 0, 0},
    {"RAMP", 1500, 60, 255},  {"OFF", 1000, 0, 0},
};
static const int STEP_COUNT = sizeof(steps) / sizeof(steps[0]);

static float pinVolts() {
  uint32_t sum = 0;
  for (int i = 0; i < 32; i++) sum += adc_read();
  return sum * 3.3f / (32 * 4096.0f);
}

// Что подключено к пину: подтягиваем его к 3.3 В слабым внутренним резистором и смотрим напряжение
static void checkWiring() {
  pinMode(PIN_VIBRO, INPUT_PULLUP);
  delay(20);
  float v = pinVolts();
  pinMode(PIN_VIBRO, OUTPUT);
  digitalWrite(PIN_VIBRO, LOW);
  Serial.printf("Пин GP%d с подтяжкой: %.2f В — ", PIN_VIBRO, v);
  if (v < 0.1f) Serial.println("пин замкнут на землю или мотор подключён напрямую, без модуля");
  else if (v < 1.5f) Serial.println("вход вибромодуля подключён");
  else if (v > 2.5f) Serial.println("к пину ничего не подключено — отошёл провод IN?");
  else Serial.println("непонятная нагрузка");
}

static void show(const char *name, uint8_t power, float volts, bool constant) {
  if (oledOk) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.printf("VIBRO TEST  GP%d", PIN_VIBRO);
    oled.setTextSize(2);
    oled.setCursor(0, 9);
    if (power) oled.printf("%s %d%%", name, power * 100 / 255);
    else oled.print("OFF");
    oled.setTextSize(1);
    oled.setCursor(0, 25);
    if (isnan(volts)) oled.printf("pin  --   %s", constant ? "BOOT=stop" : "BOOT=hold");
    else oled.printf("pin %.2fV %s", volts, constant ? "BOOT=stop" : "BOOT=hold");
    oled.display();
  }
  static int ledOn = -1;  // обновляем светодиод только при смене — show() мешает USB
  if (ledOn != (power > 0)) {
    ledOn = power > 0;
    led.fill(ledOn ? led.Color(0, 255, 0) : 0);  // зелёный — мотор крутится
    led.show();
  }
}

void setup() {
  pinMode(PIN_VIBRO, OUTPUT);
  digitalWrite(PIN_VIBRO, LOW);
  led.begin();
  led.setBrightness(LED_BRIGHTNESS);
  led.clear();
  led.show();
  Serial.begin(115200);
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 1500;) delay(10);
  Serial.println("\n=== Тест вибромотора ===");

  adc_init();
  adc_select_input(PIN_VIBRO - 26);
  bus.setSDA(I2C_SDA_PIN);
  bus.setSCL(I2C_SCL_PIN);
  bus.begin();
  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);
  if (oledOk) oled.setTextColor(SSD1306_WHITE);

  checkWiring();
  Serial.println("По кругу: FULL 1 с, 3 коротких PULSE, плавный RAMP. BOOT — включить/выключить постоянно.");
}

void loop() {
  static int idx = 0;
  static uint32_t stepStart = 0, lastShow = 0, lastButton = 0;
  static bool constant = false, wasDown = false, printed = false;
  uint32_t now = millis();

  if (now - lastButton >= 50) {  // BOOTSEL на миг блокирует прерывания — часто не опрашиваем
    lastButton = now;
    bool down = BOOTSEL;
    if (down && !wasDown) {
      constant = !constant;
      idx = 0;
      stepStart = now;
      printed = false;
      Serial.println(constant ? ">>> Мотор включён постоянно (BOOT — выключить)" : ">>> Обратно по кругу");
    }
    wasDown = down;
  }

  const char *name = "ON";
  uint8_t power = 255;
  if (!constant) {
    if (now - stepStart >= steps[idx].ms) {
      idx = (idx + 1) % STEP_COUNT;
      stepStart = now;
      printed = false;
    }
    const Step &s = steps[idx];
    name = s.name;
    power = s.from + (int)(s.to - s.from) * (int)(now - stepStart) / s.ms;
  }
  analogWrite(PIN_VIBRO, power);

  // Напряжение меряем только на полной мощности (на неполной ШИМ мигает 1000 раз в секунду)
  // и через 20 мс после включения
  uint32_t inStep = now - stepStart;
  bool full = power == 255 && inStep >= 20;
  if (!printed && power && inStep >= 60) {
    printed = true;
    if (full) Serial.printf("%-5s 100%%: на пине %.2f В\n", name, pinVolts());
    else Serial.printf("%-5s плавный разгон до 100%%\n", name);
  }
  if (now - lastShow >= 100) {
    lastShow = now;
    show(name, power, full ? pinVolts() : NAN, constant);
  }
  delay(5);
}
