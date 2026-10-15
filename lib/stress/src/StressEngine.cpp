#include "StressEngine.h"

#include <algorithm>

namespace stress {

static const float MILD_DELTA = 10, STRESS_DELTA = 25;  // пороги из проекта: норма +10 / +25
static const float EXIT_MARGIN = 3;                     // гистерезис: успокоиться — на 3 ниже порога
static const uint8_t MIN_QUALITY = 40;
static const float DECLINE_SLOPE = -0.05f;    // уд/мин в секунду — пульс идёт вниз
static const float HRV_LOW = 0.7f;
static const float HRV_BONUS = 3, SKIN_BONUS = 2, SKIN_DROP = 0.3f;
static const float MAX_ALLOWANCE = 10, ALLOWANCE_DECAY_S = 600;
static const float RECOVERY_MIN_S = 30, RECOVERY_MAX_S = 180, NEAR_BASE = 8;
static const float SIGNAL_LOSS_S = 20;  // столько без пульса — сбрасываем «жёлтый/красный»

static float median(float *v, int n) {
  if (n <= 0) return 0;
  std::sort(v, v + n);
  return v[n / 2];
}

void StressEngine::startCalibration() {
  calibrating_ = true;
  calTime_ = 0;
  calN_ = 0;
  calTemp_ = NAN;
  state_ = State::Calibrating;
}

float StressEngine::calibrationProgress() const {
  if (!calibrating_) return base_.valid() ? 1.0f : 0.0f;
  return std::min(1.0f, calTime_ / timing_.calibration);
}

void StressEngine::resetEvidence() { above1_ = above2_ = belowMild_ = belowStress_ = 0; }

void StressEngine::resetHistory() {
  hrHead_ = hrCount_ = tempHead_ = tempCount_ = 0;
  hrS_ = 0;
}

void StressEngine::pushHistory(const Inputs &in) {
  hrHist_[hrHead_] = hrS_;
  hrHead_ = (hrHead_ + 1) % HR_HIST;
  if (hrCount_ < HR_HIST) hrCount_++;
  if (std::isfinite(in.skinTemp) && (tempCount_ == 0 || in.ms - lastTempMs_ >= 10000)) {
    tempHist_[tempHead_] = in.skinTemp;
    tempHead_ = (tempHead_ + 1) % TEMP_HIST;
    if (tempCount_ < TEMP_HIST) tempCount_++;
    lastTempMs_ = in.ms;
  }
}

float StressEngine::hrSlope30() const {
  if (hrCount_ < HR_HIST) return 0;
  float now = hrHist_[(hrHead_ - 1 + HR_HIST) % HR_HIST], then = hrHist_[hrHead_];
  if (now <= 0 || then <= 0) return 0;
  return (now - then) / (HR_HIST - 1);
}

float StressEngine::skinDrop() const {  // насколько остыла кожа за 3 минуты
  if (tempCount_ < TEMP_HIST) return 0;
  return tempHist_[tempHead_] - tempHist_[(tempHead_ - 1 + TEMP_HIST) % TEMP_HIST];
}

Event StressEngine::update(const Inputs &in) {
  float dt = started_ ? (in.ms - lastMs_) / 1000.0f : 1.0f;
  if (dt <= 0 || dt > 5) dt = 1;
  started_ = true;
  lastMs_ = in.ms;

  if (!in.contact) {
    state_ = State::NoContact;
    resetEvidence();
    resetHistory();
    return Event::None;
  }

  bool good = in.bpm > 0 && in.quality >= MIN_QUALITY;
  if (good) hrS_ = hrS_ > 0 ? hrS_ + (in.bpm - hrS_) * std::min(1.0f, dt / timing_.hrTau) : in.bpm;
  pushHistory(in);

  if (!base_.valid() && !calibrating_) startCalibration();
  if (calibrating_) return calibrate(in, good, dt);

  if (in.activity == Activity::Active) {
    if (state_ != State::Active) activeSince_ = in.ms;
    state_ = State::Active;
    resetEvidence();
    return Event::None;
  }
  if (state_ == State::Active) {  // движение закончилось — восстановление
    float dur = (in.ms - activeSince_) / 1000.0f;
    allowance_ = std::max(allowance_, std::min(MAX_ALLOWANCE, dur / 6));
    recoveryLen_ = std::min(RECOVERY_MAX_S, std::max(RECOVERY_MIN_S, dur * 3));
    recoverySince_ = in.ms;
    nearBaseFor_ = 0;
    state_ = State::Recovery;
  }
  allowance_ = std::max(0.0f, allowance_ - MAX_ALLOWANCE / ALLOWANCE_DECAY_S * dt);

  if (state_ == State::Recovery) {
    nearBaseFor_ = (hrS_ > 0 && hrS_ <= base_.bpm + NEAR_BASE) ? nearBaseFor_ + dt : 0;
    if (nearBaseFor_ < 10 && (in.ms - recoverySince_) / 1000.0f < recoveryLen_) return Event::None;
    state_ = State::Calm;
    resetEvidence();
  }
  if (state_ == State::NoContact || state_ == State::Calibrating) state_ = State::Calm;
  return evaluate(in, good, dt);
}

Event StressEngine::calibrate(const Inputs &in, bool good, float dt) {
  state_ = State::Calibrating;
  if (good && in.activity != Activity::Active) {
    if (calN_ < CAL_MAX) {
      calBpm_[calN_] = in.bpm;
      calRmssd_[calN_] = in.rmssd;
      calN_++;
    }
    calTime_ += dt;
    if (std::isfinite(in.skinTemp)) calTemp_ = in.skinTemp;
  }
  if (calTime_ < timing_.calibration) return Event::None;

  base_.bpm = median(calBpm_, calN_);
  int n = 0;
  for (int i = 0; i < calN_; i++)
    if (calRmssd_[i] > 0) calRmssd_[n++] = calRmssd_[i];
  base_.rmssd = median(calRmssd_, n);
  base_.skinTemp = calTemp_;
  calibrating_ = false;
  state_ = State::Calm;
  resetEvidence();
  return Event::CalibrationDone;
}

Event StressEngine::alert(Event e, uint32_t ms) {
  bool escalation = e == Event::BecameStress && lastEvent_ == Event::BecameMild;
  if (hasEvent_ && (ms - lastEventMs_) / 1000.0f < timing_.cooldown && !escalation) return Event::None;
  hasEvent_ = true;
  lastEvent_ = e;
  lastEventMs_ = ms;
  return e;
}

Event StressEngine::evaluate(const Inputs &in, bool good, float dt) {
  if (!good) {  // нет надёжного пульса: недолго — держим состояние, долго — успокаиваемся
    noDataFor_ += dt;
    if (noDataFor_ >= SIGNAL_LOSS_S && (state_ == State::Mild || state_ == State::Stress)) {
      state_ = State::Calm;
      resetEvidence();
    }
    return Event::None;
  }
  noDataFor_ = 0;

  float hrvRatio = (in.rmssd > 0 && base_.rmssd > 0) ? in.rmssd / base_.rmssd : 1;
  float bonus = (hrvRatio <= HRV_LOW ? HRV_BONUS : 0) + (skinDrop() >= SKIN_DROP ? SKIN_BONUS : 0);
  float mildThr = MILD_DELTA - bonus + allowance_, stressThr = STRESS_DELTA - bonus + allowance_;
  float delta = hrS_ - base_.bpm;

  bool hold = hrSlope30() < DECLINE_SLOPE;  // пульс идёт вниз — восстановление, копить не будем

  int level = delta >= stressThr ? 2 : delta >= mildThr ? 1 : 0;
  int exitLevel = delta >= stressThr - EXIT_MARGIN ? 2 : delta >= mildThr - EXIT_MARGIN ? 1 : 0;
  if (level < 1) above1_ = 0;
  else if (!hold) above1_ += dt;
  if (level < 2) above2_ = 0;
  else if (!hold) above2_ += dt;
  belowMild_ = exitLevel == 0 ? belowMild_ + dt : 0;
  belowStress_ = exitLevel < 2 ? belowStress_ + dt : 0;

  Event e = Event::None;
  switch (state_) {
    case State::Calm:
      if (above1_ >= timing_.sustainMild) {
        bool strong = above2_ >= timing_.sustainStress;
        state_ = strong ? State::Stress : State::Mild;
        e = alert(strong ? Event::BecameStress : Event::BecameMild, in.ms);
      }
      break;
    case State::Mild:
      if (above2_ >= timing_.sustainStress) {
        state_ = State::Stress;
        e = alert(Event::BecameStress, in.ms);
      } else if (belowMild_ >= timing_.exitHold) {
        state_ = State::Calm;
      }
      break;
    case State::Stress:
      if (belowMild_ >= timing_.exitHold) state_ = State::Calm;
      else if (belowStress_ >= timing_.exitHold) state_ = State::Mild;
      break;
    default:
      break;
  }


  diag_ = {hrS_, delta, mildThr, stressThr, hrvRatio, allowance_, above1_, above2_, hold};
  return e;
}

}  // namespace stress
