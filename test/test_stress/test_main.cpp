#include <unity.h>

#include <cmath>
#include <functional>

#include "StressEngine.h"

using namespace stress;

// Посекундная симуляция: profile(t) задаёт входы, считаем события
struct Sim {
  StressEngine engine;
  uint32_t ms = 0;
  int mild = 0, stressed = 0, calibrated = 0;
  int firstEventAt = -1;  // секунда первого события стресса

  Sim(bool withBaseline = true) {
    if (withBaseline) engine.setBaseline({80, 40, 33.0f});
  }

  void run(int seconds, const std::function<void(int t, Inputs &in)> &profile) {
    for (int t = 0; t < seconds; t++) {
      Inputs in;
      in.ms = ms;
      in.bpm = 80;
      in.quality = 85;
      in.rmssd = 40;
      in.activity = Activity::Still;
      in.skinTemp = 33.0f;
      in.contact = true;
      profile(t, in);
      Event e = engine.update(in);
      if (e == Event::BecameMild) mild++;
      if (e == Event::BecameStress) stressed++;
      if (e == Event::CalibrationDone) calibrated++;
      if ((e == Event::BecameMild || e == Event::BecameStress) && firstEventAt < 0) firstEventAt = ms / 1000;
      ms += 1000;
    }
  }
  int events() const { return mild + stressed; }
};

void setUp() {}
void tearDown() {}

static void test_pulse_near_baseline_is_calm() {
  Sim s;
  s.run(300, [](int, Inputs &in) { in.bpm = 84; });
  TEST_ASSERT_EQUAL(State::Calm, s.engine.state());
  TEST_ASSERT_EQUAL(0, s.events());
}

static void test_rise_held_five_seconds_becomes_mild() {
  Sim s;
  s.run(60, [](int t, Inputs &in) { in.bpm = t < 10 ? 80 : 95; });
  TEST_ASSERT_EQUAL(State::Mild, s.engine.state());
  TEST_ASSERT_EQUAL(1, s.mild);
  TEST_ASSERT_TRUE(s.firstEventAt >= 15);  // не раньше чем через 5 с повышенного пульса
  TEST_ASSERT_TRUE(s.firstEventAt <= 22);
}

static void test_short_rise_is_ignored() {
  Sim s;
  s.run(60, [](int t, Inputs &in) { in.bpm = (t >= 10 && t < 13) ? 95 : 80; });  // 3 с — отпустило
  TEST_ASSERT_EQUAL(0, s.events());
}

static void test_fast_breathing_imitates_stress() {
  Sim s;
  s.run(120, [](int t, Inputs &in) {
    // ученик часто дышит 15 с — пульс поднимается и держится
    if (t >= 30 && t < 36) in.bpm = 80 + 20 * (t - 30) / 6.0f;
    else if (t >= 36 && t < 51) in.bpm = 100;
    else if (t >= 51 && t < 81) in.bpm = 100 - 20 * (t - 51) / 30.0f;
    in.rmssd = (t >= 30 && t < 81) ? 70 : 40;  // ритм «качается» — больше это не мешает
  });
  TEST_ASSERT_EQUAL(1, s.events());
  TEST_ASSERT_TRUE(s.firstEventAt > 30 && s.firstEventAt <= 45);
}

static void test_very_high_pulse_at_rest_becomes_stress_quickly() {
  Sim s;
  s.run(40, [](int t, Inputs &in) { in.bpm = t < 10 ? 80 : 112; });
  TEST_ASSERT_EQUAL(State::Stress, s.engine.state());
  TEST_ASSERT_EQUAL(1, s.stressed);
}

static void test_strong_lasting_rise_becomes_stress() {
  Sim s;
  s.run(300, [](int t, Inputs &in) {
    in.bpm = t < 90 ? 80 + 30.0f * t / 90 : 110;
    in.rmssd = 25;
  });
  TEST_ASSERT_EQUAL(State::Stress, s.engine.state());
  TEST_ASSERT_EQUAL(1, s.stressed);
}

static void test_running_and_recovery_are_not_stress() {
  Sim s;
  s.run(60, [](int, Inputs &in) {
    in.activity = Activity::Active;
    in.bpm = 150;
  });
  TEST_ASSERT_EQUAL(State::Active, s.engine.state());
  s.run(300, [](int t, Inputs &in) { in.bpm = 85 + 65 * expf(-t / 40.0f); });
  TEST_ASSERT_EQUAL(0, s.events());
  TEST_ASSERT_EQUAL(State::Calm, s.engine.state());
}

static void test_state_after_running_is_recovery() {
  Sim s;
  s.run(60, [](int, Inputs &in) {
    in.activity = Activity::Active;
    in.bpm = 150;
  });
  s.run(20, [](int, Inputs &in) { in.bpm = 140; });
  TEST_ASSERT_EQUAL(State::Recovery, s.engine.state());
}

static void test_long_intense_run_with_slow_recovery_is_not_stress() {
  Sim s;
  s.run(600, [](int, Inputs &in) {
    in.activity = Activity::Active;
    in.bpm = 165;
  });
  s.run(900, [](int t, Inputs &in) { in.bpm = 85 + 80 * expf(-t / 120.0f); });
  TEST_ASSERT_EQUAL(0, s.events());
}

static void test_pulse_staying_a_bit_high_after_running_is_not_stress() {
  Sim s;
  s.run(120, [](int, Inputs &in) {
    in.activity = Activity::Active;
    in.bpm = 150;
  });
  // после нагрузки пульс быстро падает, но ещё минуты держится на +12
  s.run(420, [](int t, Inputs &in) { in.bpm = 92 + 58 * expf(-t / 30.0f); });
  TEST_ASSERT_EQUAL(0, s.events());
}

static void test_light_gestures_do_not_block_detection() {
  Sim s;
  s.run(240, [](int t, Inputs &in) {
    in.bpm = t < 60 ? 80 + 16.0f * t / 60 : 96;
    in.rmssd = 30;
    in.activity = Activity::Light;  // жестикулирует у доски
  });
  TEST_ASSERT_EQUAL(1, s.mild);
}

static void test_poor_signal_does_not_count() {
  Sim s;
  s.run(300, [](int, Inputs &in) {
    in.bpm = 100;
    in.quality = 20;
  });
  TEST_ASSERT_EQUAL(0, s.events());
}

static void test_skin_cooling_and_low_variability_lower_threshold() {
  Sim s;
  s.run(400, [](int t, Inputs &in) {
    in.bpm = 87;                      // +7: ниже обычного порога +10
    in.rmssd = 25;                    // ВСР упала
    in.skinTemp = 33.0f - 0.004f * t; // кожа остывает — сосуды сужаются
  });
  TEST_ASSERT_EQUAL(1, s.mild);
}

static void test_same_small_rise_without_other_signs_stays_calm() {
  Sim s;
  s.run(400, [](int, Inputs &in) { in.bpm = 87; });
  TEST_ASSERT_EQUAL(0, s.events());
}

static void test_returns_to_calm_when_pulse_drops() {
  Sim s;
  s.run(200, [](int, Inputs &in) {
    in.bpm = 96;
    in.rmssd = 30;
  });
  TEST_ASSERT_EQUAL(State::Mild, s.engine.state());
  s.run(60, [](int, Inputs &in) { in.bpm = 82; });
  TEST_ASSERT_EQUAL(State::Calm, s.engine.state());
}

static void test_long_signal_loss_returns_to_calm() {
  Sim s;
  s.run(60, [](int, Inputs &in) { in.bpm = 96; });
  TEST_ASSERT_EQUAL(State::Mild, s.engine.state());
  s.run(25, [](int, Inputs &in) {  // пульс пропал (рука шевелится) — жёлтый не должен «залипать»
    in.bpm = 0;
    in.quality = 0;
  });
  TEST_ASSERT_EQUAL(State::Calm, s.engine.state());
}

static void test_short_signal_gap_keeps_state() {
  Sim s;
  s.run(60, [](int, Inputs &in) { in.bpm = 96; });
  s.run(8, [](int, Inputs &in) {
    in.bpm = 0;
    in.quality = 0;
  });
  TEST_ASSERT_EQUAL(State::Mild, s.engine.state());
}

static void test_repeated_alerts_respect_cooldown() {
  Sim s;
  Timing t;
  t.cooldown = 300;
  s.engine.setTiming(t);
  auto wave = [](int, Inputs &in) {
    in.bpm = 96;
    in.rmssd = 30;
  };
  s.run(70, wave);
  s.run(45, [](int, Inputs &in) { in.bpm = 85; });  // успокоился (без резкого скачка обратно)
  TEST_ASSERT_EQUAL(State::Calm, s.engine.state());
  s.run(70, wave);
  TEST_ASSERT_EQUAL(State::Mild, s.engine.state());  // снова волнуется — светофор жёлтый,
  TEST_ASSERT_EQUAL(1, s.events());                  // но второй раз вибрацией не будим
}

static void test_common_norm_does_not_drift_to_wearer() {
  Sim s;  // норма 80 — общая для всех, под человека не подстраивается
  s.run(1800, [](int, Inputs &in) { in.bpm = 84; });
  TEST_ASSERT_EQUAL_FLOAT(80, s.engine.baseline().bpm);
}

static void test_no_contact_state() {
  Sim s;
  s.run(5, [](int, Inputs &in) { in.contact = false; });
  TEST_ASSERT_EQUAL(State::NoContact, s.engine.state());
}

static void test_calibration_learns_baseline() {
  Sim s(false);
  s.run(1, [](int, Inputs &) {});
  TEST_ASSERT_EQUAL(State::Calibrating, s.engine.state());
  s.run(70, [](int, Inputs &in) {
    in.bpm = 78;
    in.rmssd = 45;
    in.skinTemp = 32.5f;
  });
  TEST_ASSERT_EQUAL(1, s.calibrated);
  TEST_ASSERT_FLOAT_WITHIN(1, 78, s.engine.baseline().bpm);
  TEST_ASSERT_FLOAT_WITHIN(1, 45, s.engine.baseline().rmssd);
  TEST_ASSERT_EQUAL(State::Calm, s.engine.state());
}

static void test_calibration_pauses_while_moving() {
  Sim s(false);
  s.run(30, [](int, Inputs &in) { in.bpm = 78; });
  s.run(60, [](int, Inputs &in) {
    in.bpm = 120;
    in.activity = Activity::Active;
  });
  TEST_ASSERT_EQUAL(0, s.calibrated);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.5f, s.engine.calibrationProgress());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pulse_near_baseline_is_calm);
  RUN_TEST(test_rise_held_five_seconds_becomes_mild);
  RUN_TEST(test_short_rise_is_ignored);
  RUN_TEST(test_fast_breathing_imitates_stress);
  RUN_TEST(test_very_high_pulse_at_rest_becomes_stress_quickly);
  RUN_TEST(test_strong_lasting_rise_becomes_stress);
  RUN_TEST(test_running_and_recovery_are_not_stress);
  RUN_TEST(test_state_after_running_is_recovery);
  RUN_TEST(test_long_intense_run_with_slow_recovery_is_not_stress);
  RUN_TEST(test_pulse_staying_a_bit_high_after_running_is_not_stress);
  RUN_TEST(test_light_gestures_do_not_block_detection);
  RUN_TEST(test_poor_signal_does_not_count);
  RUN_TEST(test_skin_cooling_and_low_variability_lower_threshold);
  RUN_TEST(test_same_small_rise_without_other_signs_stays_calm);
  RUN_TEST(test_returns_to_calm_when_pulse_drops);
  RUN_TEST(test_long_signal_loss_returns_to_calm);
  RUN_TEST(test_short_signal_gap_keeps_state);
  RUN_TEST(test_repeated_alerts_respect_cooldown);
  RUN_TEST(test_common_norm_does_not_drift_to_wearer);
  RUN_TEST(test_no_contact_state);
  RUN_TEST(test_calibration_learns_baseline);
  RUN_TEST(test_calibration_pauses_while_moving);
  return UNITY_END();
}
