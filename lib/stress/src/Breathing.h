#pragma once
#include <cstdint>

namespace stress {

enum class BreathPhase : uint8_t { Inhale, Hold, Exhale, Done };

// Дыхательное упражнение «4-4-4»: вдох — задержка — выдох, по phaseMs каждая фаза.
class Breathing {
 public:
  explicit Breathing(uint8_t cycles = 4, uint16_t phaseMs = 4000) : cycles_(cycles), phaseMs_(phaseMs) {}

  void start(uint32_t ms);
  void stop() { active_ = false; phase_ = BreathPhase::Done; }
  // Возвращает true, если фаза сменилась (в т.ч. самый первый вдох и окончание).
  bool update(uint32_t ms);

  bool active() const { return active_; }
  BreathPhase phase() const { return phase_; }
  uint8_t cycle() const { return cycle_; }  // 1..cycles
  uint8_t cycles() const { return cycles_; }
  uint8_t secondsLeft() const { return secondsLeft_; }
  float lungs() const;  // 0..1 «объём лёгких»: вдох 0→1, задержка 1, выдох 1→0

 private:
  uint8_t cycles_;
  uint16_t phaseMs_;
  bool active_ = false, started_ = false;
  uint32_t startMs_ = 0;
  BreathPhase phase_ = BreathPhase::Done;
  uint8_t cycle_ = 0, secondsLeft_ = 0;
  float progress_ = 0;
};

}  // namespace stress
