#include "Breathing.h"

namespace stress {

void Breathing::start(uint32_t ms) {
  startMs_ = ms;
  active_ = true;
  started_ = false;
  phase_ = BreathPhase::Inhale;
  cycle_ = 1;
  secondsLeft_ = phaseMs_ / 1000;
  progress_ = 0;
}

bool Breathing::update(uint32_t ms) {
  if (!active_) return false;
  uint32_t elapsed = ms - startMs_;
  uint32_t total = (uint32_t)cycles_ * 3 * phaseMs_;
  if (elapsed >= total) {
    active_ = false;
    phase_ = BreathPhase::Done;
    return true;
  }
  uint32_t index = elapsed / phaseMs_;
  uint32_t inPhase = elapsed % phaseMs_;
  BreathPhase phase = (BreathPhase)(index % 3);
  bool changed = !started_ || phase != phase_;
  started_ = true;
  phase_ = phase;
  cycle_ = index / 3 + 1;
  progress_ = (float)inPhase / phaseMs_;
  secondsLeft_ = (phaseMs_ - inPhase + 999) / 1000;
  return changed;
}

float Breathing::lungs() const {
  switch (phase_) {
    case BreathPhase::Inhale: return progress_;
    case BreathPhase::Hold: return 1.0f;
    case BreathPhase::Exhale: return 1.0f - progress_;
    default: return 0.0f;
  }
}

}  // namespace stress
