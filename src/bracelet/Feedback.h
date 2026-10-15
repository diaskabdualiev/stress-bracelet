#pragma once
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

struct VibroStep {
  uint16_t onMs, offMs;
  uint8_t power;  // 0..255
};

// Вибромотор: проигрывает узор (включить/выключить), не останавливая программу.
class Vibro {
 public:
  explicit Vibro(uint8_t pin) : pin_(pin) {}
  void begin();
  void play(const VibroStep *steps, uint8_t count);
  template <size_t N>
  void play(const VibroStep (&steps)[N]) { play(steps, N); }
  void update(uint32_t ms);
  bool busy() const { return count_ > 0; }

 private:
  void start(uint32_t ms);
  uint8_t pin_;
  const VibroStep *steps_ = nullptr;
  uint8_t count_ = 0, index_ = 0;
  bool on_ = false;
  uint32_t until_ = 0;
};

namespace vibe {
extern const VibroStep tick[1];        // нажатие кнопки
extern const VibroStep mild[3];        // жёлтый: три мягких импульса
extern const VibroStep stress[5];      // красный: два длинных и три коротких (~3 с)
extern const VibroStep inhale[1];      // упражнение: начало вдоха
extern const VibroStep hold[1];        //             задержка
extern const VibroStep exhale[2];      //             выдох
extern const VibroStep done[1];        // упражнение закончено
extern const VibroStep calibrated[2];  // норма записана
}  // namespace vibe

// Адресные светодиоды: один цвет на все.
class StatusLed {
 public:
  StatusLed(uint16_t count, uint8_t pin) : strip_(count, pin, NEO_GRB + NEO_KHZ800) {}
  void begin(uint8_t brightness);
  void show(uint8_t r, uint8_t g, uint8_t b, float level);  // level 0..1

 private:
  Adafruit_NeoPixel strip_;
};
