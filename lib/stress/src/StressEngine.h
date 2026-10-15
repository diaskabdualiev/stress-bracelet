#pragma once
#include <cmath>
#include <cstdint>

#include "MotionTracker.h"

namespace stress {

enum class State : uint8_t { NoContact, Calibrating, Calm, Mild, Stress, Active, Recovery };
enum class Event : uint8_t { None, BecameMild, BecameStress, CalibrationDone };

struct Inputs {
  uint32_t ms = 0;
  float bpm = 0;        // 0 — пульс неизвестен
  uint8_t quality = 0;  // качество сигнала 0..100
  float rmssd = 0;      // вариабельность ритма, мс; 0 — неизвестна
  Activity activity = Activity::Still;
  float skinTemp = NAN;  // температура кожи, °C
  bool contact = false;
};

// Индивидуальная норма ребёнка в спокойном состоянии
struct Baseline {
  float bpm = 0, rmssd = 0, skinTemp = NAN;
  bool valid() const { return bpm > 0; }
};

struct Timing {  // секунды
  float sustainMild = 5;     // столько пульс должен держаться повышенным для «жёлтого»
  float sustainStress = 5;   // и сильно повышенным — для «красного»
  float exitHold = 10;       // столько держаться ниже порога, чтобы успокоиться
  float cooldown = 30;       // повторное напоминание не раньше
  float calibration = 60;
  float hrTau = 3;           // сглаживание пульса
};

// Почему движок решил так, а не иначе — для вывода в Serial
struct Diagnostics {
  float hr = 0, delta = 0, mildThr = 0, stressThr = 0, hrvRatio = 1, allowance = 0, above1 = 0, above2 = 0;
  bool declining = false;
};

// Светофор стресса: 🟢 спокойно, 🟡 волнение (норма +10), 🔴 стресс (норма +25).
// Стресс — пульс выше нормы держится 5 секунд (так его можно показать, часто подышав).
// Но пульс растёт и от нагрузки, поэтому:
//   * движение (бег, прыжки, ходьба) — не стресс; после него «Отдых», пока пульс не вернётся,
//     и ещё 10 минут поправка к порогам: после нагрузки пульс долго немного повышен;
//   * пульс снижается — организм восстанавливается, это не стресс;
//   * низкая вариабельность ритма и остывающая кожа (сосуды сужаются) — признаки стресса,
//     порог чуть ниже.
class StressEngine {
 public:
  void setTiming(const Timing &t) { timing_ = t; }
  const Timing &timing() const { return timing_; }
  void setBaseline(const Baseline &b) {
    base_ = b;
    calibrating_ = false;
  }
  const Baseline &baseline() const { return base_; }
  void startCalibration();

  Event update(const Inputs &in);  // вызывать раз в секунду

  State state() const { return state_; }
  float calibrationProgress() const;  // 0..1
  const Diagnostics &diagnostics() const { return diag_; }

 private:
  static const int CAL_MAX = 120, HR_HIST = 31, TEMP_HIST = 19;

  Event calibrate(const Inputs &in, bool good, float dt);
  Event evaluate(const Inputs &in, bool good, float dt);
  Event alert(Event e, uint32_t ms);
  void resetEvidence();
  void resetHistory();
  void pushHistory(const Inputs &in);
  float hrSlope30() const;
  float skinDrop() const;

  Timing timing_;
  Baseline base_;
  State state_ = State::NoContact;
  bool started_ = false, calibrating_ = false, hasEvent_ = false;
  uint32_t lastMs_ = 0, activeSince_ = 0, recoverySince_ = 0, lastEventMs_ = 0, lastTempMs_ = 0;
  Event lastEvent_ = Event::None;
  float hrS_ = 0, allowance_ = 0, recoveryLen_ = 0, nearBaseFor_ = 0;
  float above1_ = 0, above2_ = 0, belowMild_ = 0, belowStress_ = 0, noDataFor_ = 0;

  float calBpm_[CAL_MAX], calRmssd_[CAL_MAX], calTime_ = 0, calTemp_ = NAN;
  int calN_ = 0;

  float hrHist_[HR_HIST], tempHist_[TEMP_HIST];
  int hrHead_ = 0, hrCount_ = 0, tempHead_ = 0, tempCount_ = 0;

  Diagnostics diag_;
};

}  // namespace stress
