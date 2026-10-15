#include "Display.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace ui {

using stress::BreathPhase;
using stress::State;

static const uint16_t ON = 1, OFF = 0;
static const int CX = 16;  // центр по ширине

// ---------------------------------------------------------------- примитивы

// Текст по центру; если не влезает в 32 точки — сжимаем расстояние между буквами
static void text(Adafruit_GFX &g, const char *s, int y, uint8_t size) {
  int n = strlen(s), glyph = 5 * size, step = 6 * size;
  if (n > 1 && n * step - size > g.width()) step = glyph + (g.width() - n * glyph) / (n - 1);
  int x = (g.width() - ((n - 1) * step + glyph)) / 2;
  for (int i = 0; i < n; i++) g.drawChar(x + i * step, y, s[i], ON, OFF, size);
}

static void heart(Adafruit_GFX &g, int cx, int cy, int r, uint16_t c) {
  g.fillCircle(cx - r, cy, r, c);
  g.fillCircle(cx + r, cy, r, c);
  g.fillTriangle(cx - 2 * r, cy + 1, cx + 2 * r, cy + 1, cx, cy + 2 * r + 2, c);
}

static void arc(Adafruit_GFX &g, int cx, int cy, int r, int fromDeg, int toDeg) {
  for (int a = fromDeg; a <= toDeg; a += 4) {
    float rad = a * 3.14159265f / 180;
    g.drawPixel(cx + (int)lroundf(r * cosf(rad)), cy + (int)lroundf(r * sinf(rad)), ON);
  }
}

static void thickLine(Adafruit_GFX &g, int x0, int y0, int x1, int y1) {
  g.drawLine(x0, y0, x1, y1, ON);
  g.drawLine(x0 + 1, y0, x1 + 1, y1, ON);
}

static void signalBars(Adafruit_GFX &g, uint8_t quality) {
  for (int i = 0; i < 3; i++) {
    int h = 4 + 3 * i, x = 21 + 4 * i;
    if (quality >= 25 * (i + 1)) g.fillRect(x, 12 - h, 2, h, ON);
    else g.drawFastHLine(x, 11, 2, ON);
  }
}

static void thermometer(Adafruit_GFX &g, int y) {
  g.drawRoundRect(CX - 3, y, 7, 13, 3, ON);
  g.fillCircle(CX, y + 13, 4, ON);
  g.fillRect(CX - 1, y + 5, 3, 7, ON);
}

static void temperature(Adafruit_GFX &g, float t, int y) {
  char buf[8];
  if (isnan(t)) strcpy(buf, "--.-");
  else snprintf(buf, sizeof buf, "%.1f", t);
  int w = strlen(buf) * 6 - 1, x = (g.width() - (w + 4)) / 2;
  g.setTextSize(1);
  g.setTextColor(ON);
  g.setCursor(x, y);
  g.print(buf);
  g.drawCircle(x + w + 3, y + 1, 1, ON);  // знак градуса
}

// ---------------------------------------------------------------- лицо

enum class Face { Happy, Neutral, Worried, Blank, Thinking, Relaxed };

static void eyes(Adafruit_GFX &g, int y, int dx = 0) {
  g.fillRect(CX - 6 + dx, y - 1, 2, 3, ON);
  g.fillRect(CX + 5 + dx, y - 1, 2, 3, ON);
}

static void face(Adafruit_GFX &g, Face f, int cy) {
  g.drawCircle(CX, cy, 14, ON);
  g.drawCircle(CX, cy, 13, ON);
  int ey = cy - 4;
  switch (f) {
    case Face::Happy:
      eyes(g, ey);
      arc(g, CX, cy, 7, 25, 155);
      arc(g, CX, cy, 6, 25, 155);
      break;
    case Face::Neutral:
      eyes(g, ey);
      g.fillRect(CX - 5, cy + 5, 11, 2, ON);
      break;
    case Face::Worried:
      eyes(g, ey + 1);
      g.drawLine(CX - 8, ey - 2, CX - 3, ey - 4, ON);  // брови «домиком»
      g.drawLine(CX + 3, ey - 4, CX + 8, ey - 2, ON);
      arc(g, CX, cy + 12, 6, 215, 325);
      arc(g, CX, cy + 13, 6, 215, 325);
      g.fillCircle(CX + 11, cy - 10, 2, ON);  // капля пота
      g.fillTriangle(CX + 9, cy - 10, CX + 13, cy - 10, CX + 11, cy - 15, ON);
      break;
    case Face::Blank:
      g.drawFastHLine(CX - 7, ey, 4, ON);
      g.drawFastHLine(CX + 4, ey, 4, ON);
      g.drawFastHLine(CX - 3, cy + 6, 7, ON);
      break;
    case Face::Thinking:
      eyes(g, ey - 1, 1);
      g.drawCircle(CX, cy + 6, 2, ON);
      break;
    case Face::Relaxed:  // глаза-дужки ^ ^
      arc(g, CX - 5, ey + 1, 2, 200, 340);
      arc(g, CX + 5, ey + 1, 2, 200, 340);
      arc(g, CX, cy, 7, 30, 150);
      break;
  }
}

static void runner(Adafruit_GFX &g, int cy) {  // бегущий человечек вместо лица
  int y = cy - 14;
  g.fillCircle(19, y + 3, 3, ON);
  thickLine(g, 18, y + 7, 15, y + 17);   // туловище
  thickLine(g, 17, y + 9, 22, y + 13);   // рука вперёд
  thickLine(g, 22, y + 13, 25, y + 10);
  thickLine(g, 17, y + 9, 11, y + 12);   // рука назад
  thickLine(g, 15, y + 17, 20, y + 22);  // нога вперёд
  thickLine(g, 20, y + 22, 19, y + 28);
  thickLine(g, 15, y + 17, 10, y + 23);  // нога назад
  thickLine(g, 10, y + 23, 5, y + 22);
}

static void trafficLight(Adafruit_GFX &g, int level, int y) {  // светофор: 0 зелёный, 1 жёлтый, 2 красный
  for (int i = 0; i < 3; i++) {
    int x = 6 + 10 * i;
    if (i == level) g.fillCircle(x, y, 4, ON);
    else g.drawCircle(x, y, 3, ON);
  }
}

// ---------------------------------------------------------------- экраны

void drawMain(Adafruit_GFX &g, const MainView &v) {
  g.fillScreen(OFF);

  // сердце + качество сигнала
  bool known = v.bpm > 0;
  if (!known && v.state == State::NoContact) {  // пустое сердце — браслет не на руке
    heart(g, 8, 4, 3, ON);
    heart(g, 8, 4, 2, OFF);
  } else {
    heart(g, 8, 4, v.beat ? 4 : 3, ON);
  }
  signalBars(g, v.quality);

  // пульс; на руке, но пульса пока нет — бегущие точки: «ищу пульс», а не «завис»
  char buf[8];
  if (known) {
    snprintf(buf, sizeof buf, "%d", v.bpm);
    text(g, buf, 17, 2);
  } else if (v.state != State::NoContact) {
    for (int i = 0; i < 3; i++) {
      if (i == v.frame % 3) g.fillCircle(6 + 10 * i, 24, 3, ON);
      else g.fillCircle(6 + 10 * i, 24, 1, ON);
    }
  } else {
    text(g, "--", 17, 2);
  }
  text(g, "bpm", 35, 1);

  // температура
  thermometer(g, 46);
  temperature(g, v.temp, 66);

  // состояние
  const int fy = 93;
  switch (v.state) {
    case State::Calm: face(g, Face::Happy, fy); trafficLight(g, 0, 119); break;
    case State::Mild: face(g, Face::Neutral, fy); trafficLight(g, 1, 119); break;
    case State::Stress: face(g, Face::Worried, fy); trafficLight(g, 2, 119); break;
    case State::Active: runner(g, fy); text(g, "MOVE", 114, 1); break;
    case State::Recovery: face(g, Face::Relaxed, fy); text(g, "REST", 114, 1); break;
    case State::Calibrating:
      face(g, Face::Thinking, fy);
      text(g, "CAL", 112, 1);
      g.drawRect(2, 122, 28, 5, ON);
      g.fillRect(3, 123, (int)(26 * v.calibration), 3, ON);
      break;
    case State::NoContact: face(g, Face::Blank, fy); break;
  }
}

void drawBreathing(Adafruit_GFX &g, const stress::Breathing &b, int bpm) {
  g.fillScreen(OFF);
  char buf[8];
  snprintf(buf, sizeof buf, "%d/%d", b.cycle(), b.cycles());
  text(g, buf, 0, 1);

  // «лёгкие»: круг растёт на вдохе, замирает на задержке, сжимается на выдохе
  int r = 4 + (int)lroundf(b.lungs() * 11);
  g.fillCircle(CX, 27, r, ON);
  if (b.phase() == BreathPhase::Hold) g.fillCircle(CX, 27, r - 3, OFF);

  snprintf(buf, sizeof buf, "%d", b.secondsLeft());
  text(g, buf, 48, 3);

  int y = 76;
  switch (b.phase()) {
    case BreathPhase::Inhale:  // стрелка вверх — вдох
      g.fillTriangle(CX, y, CX - 9, y + 10, CX + 9, y + 10, ON);
      g.fillRect(CX - 3, y + 10, 7, 10, ON);
      break;
    case BreathPhase::Hold:  // пауза — задержка
      g.fillRect(CX - 8, y + 2, 5, 18, ON);
      g.fillRect(CX + 4, y + 2, 5, 18, ON);
      break;
    case BreathPhase::Exhale:  // стрелка вниз — выдох
      g.fillRect(CX - 3, y, 7, 10, ON);
      g.fillTriangle(CX, y + 20, CX - 9, y + 10, CX + 9, y + 10, ON);
      break;
    default: break;
  }

  heart(g, 7, 115, 2, ON);
  g.setTextSize(1);
  g.setTextColor(ON);
  g.setCursor(14, 113);
  if (bpm > 0) g.print(bpm);
  else g.print("--");
}

void drawResult(Adafruit_GFX &g, int before, int after) {
  g.fillScreen(OFF);
  heart(g, 8, 4, 3, ON);
  char buf[8];
  if (before > 0 && after > 0) {
    snprintf(buf, sizeof buf, "%d", before);
    text(g, buf, 17, 2);
    bool down = after <= before;  // стрелка: пульс снизился / вырос
    if (down) g.fillTriangle(CX, 46, CX - 6, 39, CX + 6, 39, ON);
    else g.fillTriangle(CX, 39, CX - 6, 46, CX + 6, 46, ON);
    snprintf(buf, sizeof buf, "%d", after);
    text(g, buf, 50, 2);
    snprintf(buf, sizeof buf, "%+d", after - before);
    text(g, buf, 112, 1);
  } else {
    text(g, "OK", 30, 2);
  }
  face(g, Face::Happy, 88);
}

void drawSplash(Adafruit_GFX &g) {
  g.fillScreen(OFF);
  heart(g, CX, 40, 6, ON);
  text(g, "HI!", 70, 2);
}

}  // namespace ui
