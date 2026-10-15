#include "Feedback.h"

void Vibro::begin() {
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
}

void Vibro::play(const VibroStep *steps, uint8_t count) {
  steps_ = steps;
  count_ = count;
  index_ = 0;
  start(millis());
}

void Vibro::start(uint32_t ms) {
  analogWrite(pin_, steps_[index_].power);
  on_ = true;
  until_ = ms + steps_[index_].onMs;
}

void Vibro::update(uint32_t ms) {
  if (!count_ || (int32_t)(ms - until_) < 0) return;
  if (on_) {
    analogWrite(pin_, 0);
    on_ = false;
    until_ = ms + steps_[index_].offMs;
    return;
  }
  if (++index_ >= count_) {
    count_ = 0;
    return;
  }
  start(ms);
}

namespace vibe {
// Маленькому мотору нужно 50–100 мс, чтобы раскрутиться: короче и слабее — почти не ощущается
const VibroStep tick[1] = {{100, 0, 255}};
const VibroStep mild[3] = {{250, 200, 255}, {250, 200, 255}, {250, 0, 255}};
const VibroStep stress[5] = {{700, 300, 255}, {700, 300, 255}, {200, 150, 255}, {200, 150, 255}, {200, 0, 255}};
const VibroStep inhale[1] = {{250, 0, 255}};
const VibroStep hold[1] = {{120, 0, 255}};
const VibroStep exhale[2] = {{150, 120, 255}, {150, 0, 255}};
const VibroStep done[1] = {{600, 0, 255}};
const VibroStep calibrated[2] = {{200, 150, 255}, {200, 0, 255}};
}  // namespace vibe

void StatusLed::begin(uint8_t brightness) {
  strip_.begin();
  strip_.setBrightness(brightness);
  strip_.clear();
  strip_.show();
}

void StatusLed::show(uint8_t r, uint8_t g, uint8_t b, float level) {
  level = constrain(level, 0.0f, 1.0f);
  strip_.fill(strip_.Color((uint8_t)(r * level), (uint8_t)(g * level), (uint8_t)(b * level)));
  strip_.show();
}
