#pragma once
#include <Adafruit_GFX.h>

#include "Breathing.h"
#include "StressEngine.h"

// Экраны браслета. Рисуют на экране, повёрнутом вертикально: 32 точки в ширину, 128 в высоту.
// Только значки и цифры — встроенный шрифт не умеет русские буквы.
namespace ui {

struct MainView {
  int bpm = 0;            // 0 — «--»
  uint8_t quality = 0;    // качество сигнала 0..100 (полоски у сердца)
  bool beat = false;      // только что был удар — сердце «бьётся»
  float temp = NAN;       // °C; NAN — «--.-» (датчик ещё прогревается)
  stress::State state = stress::State::NoContact;
  float calibration = 0;  // 0..1
  uint8_t frame = 0;      // счётчик кадров — для анимации «ищу пульс»
};

void drawMain(Adafruit_GFX &g, const MainView &v);
void drawBreathing(Adafruit_GFX &g, const stress::Breathing &b, int bpm);
void drawResult(Adafruit_GFX &g, int before, int after);
void drawSplash(Adafruit_GFX &g);

}  // namespace ui
