#pragma once
#include <cstdint>

namespace stress {

enum class Activity : uint8_t { Still, Light, Active };

// Насколько активно двигается рука: по модулю ускорения (без гравитации) и скорости вращения.
//   Still  — рука в покое: можно мерить пульс и считать стресс;
//   Light  — жесты, письмо: пульс может «шуметь»;
//   Active — ходьба, бег, прыжки: пульс растёт от нагрузки, это не стресс.
class MotionTracker {
 public:
  explicit MotionTracker(float fs = 50.0f);
  void addSample(float ax, float ay, float az, float gx, float gy, float gz);  // g и °/с

  Activity activity() const { return activity_; }
  float accelRms() const;  // g
  float gyroRms() const;   // °/с

 private:
  float dt_, aGrav_, aVar_, aBias_;
  bool init_ = false;
  float grav_ = 1, accVar_ = 0, gyroVar_ = 0, bx_ = 0, by_ = 0, bz_ = 0;
  float rawActiveFor_ = 0, notActiveFor_ = 0, rawStillFor_ = 0;
  Activity activity_ = Activity::Still;
};

}  // namespace stress
