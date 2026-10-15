#include "MotionTracker.h"

#include <cmath>

namespace stress {

static const float LIGHT_ACC = 0.02f, LIGHT_GYRO = 20.0f;     // g, °/с
static const float ACTIVE_ACC = 0.12f, ACTIVE_GYRO = 120.0f;
static const float ENTER_ACTIVE_S = 2.5f;  // короткий толчок — ещё не активность
static const float EXIT_ACTIVE_S = 3.0f;
static const float STILL_HOLD_S = 2.0f;

MotionTracker::MotionTracker(float fs)
    : dt_(1.0f / fs),
      aGrav_(1.0f / (1.0f * fs)),   // «гравитация» = среднее за ~1 с
      aVar_(1.0f / (0.25f * fs)),   // энергия движения (RMS спадает вдвое медленнее — ~0.5 с)
      aBias_(1.0f / (4.0f * fs)) {} // дрейф нуля гироскопа за ~4 с

void MotionTracker::addSample(float ax, float ay, float az, float gx, float gy, float gz) {
  float m = sqrtf(ax * ax + ay * ay + az * az);
  if (!init_) {  // ноль гироскопа начинаем с 0: первый отсчёт может прийтись на движение
    grav_ = m;
    init_ = true;
  }
  grav_ += (m - grav_) * aGrav_;
  float dyn = m - grav_;
  accVar_ += (dyn * dyn - accVar_) * aVar_;

  // Ноль гироскопа подстраиваем, только когда рука почти не вращается — иначе движение
  // «впитается» в ноль и после него рука ещё секунды будет казаться движущейся.
  // В движении — очень медленно, чтобы не залипнуть, если старт был на ходу.
  float hx = gx - bx_, hy = gy - by_, hz = gz - bz_;
  float a = (hx * hx + hy * hy + hz * hz < 30.0f * 30.0f) ? aBias_ : aBias_ * 0.1f;
  bx_ += hx * a;
  by_ += hy * a;
  bz_ += hz * a;
  hx = gx - bx_;
  hy = gy - by_;
  hz = gz - bz_;
  gyroVar_ += ((hx * hx + hy * hy + hz * hz) - gyroVar_) * aVar_;

  float acc = accelRms(), gyro = gyroRms();
  Activity raw = (acc > ACTIVE_ACC || gyro > ACTIVE_GYRO) ? Activity::Active
                 : (acc > LIGHT_ACC || gyro > LIGHT_GYRO) ? Activity::Light
                                                          : Activity::Still;
  rawActiveFor_ = raw == Activity::Active ? rawActiveFor_ + dt_ : 0;
  notActiveFor_ = raw != Activity::Active ? notActiveFor_ + dt_ : 0;
  rawStillFor_ = raw == Activity::Still ? rawStillFor_ + dt_ : 0;

  if (activity_ == Activity::Active) {
    if (notActiveFor_ >= EXIT_ACTIVE_S) activity_ = Activity::Light;
  } else if (rawActiveFor_ >= ENTER_ACTIVE_S) {
    activity_ = Activity::Active;
  } else if (raw != Activity::Still) {
    activity_ = Activity::Light;
  }
  if (activity_ == Activity::Light && rawStillFor_ >= STILL_HOLD_S) activity_ = Activity::Still;
}

float MotionTracker::accelRms() const { return sqrtf(accVar_); }
float MotionTracker::gyroRms() const { return sqrtf(gyroVar_); }

}  // namespace stress
