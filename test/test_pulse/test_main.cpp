#include <unity.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "PulseDetector.h"

using namespace stress;

static const float FS = 100.0f;
static const float PI_F = 3.14159265f;
static uint32_t rng = 1;

static float noise(float amp) {
  rng = rng * 1103515245u + 12345u;
  return ((rng >> 8) / 16777216.0f * 2.0f - 1.0f) * amp;
}

// Искусственный ИК-сигнал с запястья. Чем больше крови под датчиком, тем меньше отражённого
// света, поэтому систола — провал. Есть дикротическая волна, дыхательный дрейф и шум.
struct Ppg {
  float bpm = 72, dc = 120000, ac = 300, dicrotic = 0.4f, noiseAmp = 20, respAmp = 600;
  float hrvMs = 0;  // чередование интервалов ±hrvMs
  float t = 0, phase = 0;
  bool longBeat = false;

  uint32_t next() {
    float ibi = 60.0f / bpm + (longBeat ? hrvMs : -hrvMs) / 1000.0f;
    phase += 1.0f / (FS * ibi);
    if (phase >= 1.0f) {
      phase -= 1.0f;
      longBeat = !longBeat;
    }
    float w = expf(-powf((phase - 0.15f) / 0.07f, 2)) + dicrotic * expf(-powf((phase - 0.45f) / 0.08f, 2));
    t += 1.0f / FS;
    float v = dc - ac * w + respAmp * sinf(2 * PI_F * 0.25f * t) + noise(noiseAmp);
    return (uint32_t)fmaxf(v, 0);
  }
};

static int feed(PulseDetector &p, Ppg &g, float seconds) {
  int beats = 0;
  for (int i = 0; i < seconds * FS; i++) beats += p.addSample(g.next());
  return beats;
}

void setUp() { rng = 1; }
void tearDown() {}

static void test_no_contact_without_skin() {
  PulseDetector p(FS);
  for (int i = 0; i < 10 * FS; i++) p.addSample(1500 + (uint32_t)(noise(200) + 200));
  TEST_ASSERT_FALSE(p.contact());
  TEST_ASSERT_EQUAL_FLOAT(0, p.bpm());
}

static void test_measures_resting_pulse() {
  PulseDetector p(FS);
  Ppg g;
  feed(p, g, 15);
  TEST_ASSERT_TRUE(p.contact());
  TEST_ASSERT_FLOAT_WITHIN(3, 72, p.bpm());
  TEST_ASSERT_GREATER_OR_EQUAL_UINT8(60, p.quality());
}

static void test_first_value_appears_within_five_seconds() {
  PulseDetector p(FS);
  Ppg g;
  feed(p, g, 5);
  TEST_ASSERT_FLOAT_WITHIN(4, 72, p.bpm());
}

static void test_pulse_comes_back_within_six_seconds_after_motion() {
  PulseDetector p(FS);
  Ppg g;
  feed(p, g, 15);
  p.setMotion(true);
  feed(p, g, 5);
  p.setMotion(false);
  g.bpm = 95;
  feed(p, g, 6);
  TEST_ASSERT_FLOAT_WITHIN(6, 95, p.bpm());
}

static void test_measures_fast_pulse_after_running() {
  PulseDetector p(FS);
  Ppg g;
  g.bpm = 150;
  feed(p, g, 15);
  TEST_ASSERT_FLOAT_WITHIN(4, 150, p.bpm());
}

static void test_measures_slow_pulse() {
  PulseDetector p(FS);
  Ppg g;
  g.bpm = 52;
  feed(p, g, 18);
  TEST_ASSERT_FLOAT_WITHIN(3, 52, p.bpm());
}

static void test_weak_wrist_signal() {
  PulseDetector p(FS);
  Ppg g;
  g.ac = 70;
  g.noiseAmp = 25;
  feed(p, g, 18);
  TEST_ASSERT_FLOAT_WITHIN(4, 72, p.bpm());
}

static void test_strong_dicrotic_wave_is_not_counted_twice() {
  PulseDetector p(FS);
  Ppg g;
  g.bpm = 60;
  g.dicrotic = 0.75f;
  int beats = feed(p, g, 30);
  TEST_ASSERT_FLOAT_WITHIN(3, 60, p.bpm());
  TEST_ASSERT_INT_WITHIN(3, 30, beats);
}

static void test_noise_without_pulse_is_not_reported() {
  PulseDetector p(FS);
  Ppg g;
  g.ac = 0;
  g.noiseAmp = 60;
  feed(p, g, 20);
  TEST_ASSERT_TRUE(p.contact());
  TEST_ASSERT_EQUAL_FLOAT(0, p.bpm());
  TEST_ASSERT_LESS_THAN_UINT8(40, p.quality());
}

static void test_tracks_rising_pulse() {
  PulseDetector p(FS);
  Ppg g;
  g.bpm = 70;
  feed(p, g, 15);
  g.bpm = 100;
  feed(p, g, 20);
  TEST_ASSERT_FLOAT_WITHIN(4, 100, p.bpm());
}

static void test_counts_individual_beats() {
  PulseDetector p(FS);
  Ppg g;
  feed(p, g, 5);
  int beats = feed(p, g, 30);
  TEST_ASSERT_INT_WITHIN(2, 36, beats);
}

static void test_rmssd_reflects_beat_to_beat_variability() {
  PulseDetector p(FS);
  Ppg g;
  g.bpm = 75;
  g.hrvMs = 25;  // интервалы 775/825 мс → соседние разности 50 мс
  feed(p, g, 40);
  TEST_ASSERT_FLOAT_WITHIN(15, 50, p.rmssd());
}

static void test_rmssd_is_low_for_steady_rhythm() {
  PulseDetector p(FS);
  Ppg g;
  g.bpm = 75;
  feed(p, g, 40);
  TEST_ASSERT_TRUE(p.rmssd() > 0);
  TEST_ASSERT_LESS_THAN_FLOAT(20, p.rmssd());
}

static void test_rmssd_stays_realistic_on_noisy_weak_signal() {
  PulseDetector p(FS);
  Ppg g;
  g.bpm = 75;
  g.ac = 45;
  g.noiseAmp = 45;  // слабый сигнал с запястья: удары «дрожат» из-за шума
  feed(p, g, 60);
  TEST_ASSERT_LESS_THAN_FLOAT(60, p.rmssd());  // ровный ритм — не сотни мс
}

static void test_led_current_change_does_not_inflate_rmssd() {
  PulseDetector p(FS);
  Ppg g;
  g.bpm = 75;
  feed(p, g, 20);
  for (int i = 0; i < 3; i++) {  // автоподстройка яркости при надевании
    g.dc += 25000;
    g.ac *= 1.2f;
    p.notifyGainChange();
    feed(p, g, 3);
  }
  feed(p, g, 5);
  TEST_ASSERT_LESS_THAN_FLOAT(20, p.rmssd());
}

// Браслет болтается: датчик то прижимается, то отходит — нерегулярные «горбы» (интервалы 0.6–1.8 с).
// Отрезок ровных горбов умеренного размаха от пульса не отличить ничем — это проверяют другие тесты.
static void test_loose_strap_artifacts_are_not_taken_for_pulse() {
  PulseDetector p(FS);
  std::vector<std::pair<float, float>> bumps;
  for (float t = 0; t < 120; t += 0.6f + (noise(1) + 1) * 0.6f) bumps.push_back({t, 500 + (noise(1) + 1) * 1000});
  int secondsWithPulse = 0;
  for (int i = 0; i < 120 * FS; i++) {
    float t = i / FS, v = 50000 + 800 * sinf(2 * PI_F * 0.05f * t);
    for (auto &b : bumps)
      if (fabsf(t - b.first) < 0.6f) v += b.second * expf(-powf((t - b.first) / 0.15f, 2));
    p.addSample((uint32_t)(v + noise(20)));
    if (i % (int)FS == 0 && p.bpm() > 0) secondsWithPulse++;
  }
  TEST_ASSERT_LESS_OR_EQUAL(5, secondsWithPulse);
}

// Датчик то прижимается, то отходит прямо в «пульсовом» ритме — размах в сотни раз больше пульса
static void test_pressure_artifacts_in_pulse_range_are_rejected() {
  PulseDetector p(FS);
  std::vector<std::pair<float, float>> bumps;
  for (float t = 0; t < 90; t += 0.8f + (noise(1) + 1) * 0.15f) bumps.push_back({t, 1500 + (noise(1) + 1) * 1250});
  int secondsWithPulse = 0;
  for (int i = 0; i < 90 * FS; i++) {
    float t = i / FS, v = 50000;
    for (auto &b : bumps)
      if (fabsf(t - b.first) < 0.6f) v += b.second * expf(-powf((t - b.first) / 0.12f, 2));
    p.addSample((uint32_t)(v + noise(20)));
    if (i % (int)FS == 0 && p.bpm() > 0) secondsWithPulse++;
  }
  TEST_ASSERT_LESS_OR_EQUAL(3, secondsWithPulse);
}

static int replaySecondsWithPulse(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return -1;
  PulseDetector p(FS);
  unsigned long ir, red;
  int n = 0, seconds = 0;
  while (fscanf(f, "%lu,%lu", &ir, &red) == 2)
    if (p.addSample((uint32_t)ir), ++n % (int)FS == 0 && p.bpm() > 0) seconds++;
  fclose(f);
  return seconds;
}

// Настоящая запись: браслет болтался на руке — пульса там не видно, выдумывать нельзя
static void test_real_loose_strap_recording() {
  int s = replaySecondsWithPulse("test/data/wrist_loose_artifacts.csv");
  TEST_ASSERT_NOT_EQUAL_MESSAGE(-1, s, "нет файла test/data/wrist_loose_artifacts.csv");
  TEST_ASSERT_LESS_OR_EQUAL(3, s);
}

// Настоящая запись с тыльной стороны запястья (MAX30102, ИК). По спектру ИК и красного
// каналов пульс 83–89 уд/мин.
static void test_real_wrist_recording() {
  FILE *f = fopen("test/data/wrist_still_86bpm.csv", "r");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, "нет файла test/data/wrist_still_86bpm.csv");
  PulseDetector p(FS);
  unsigned long ir, red;
  int n = 0, okSeconds = 0, reported = 0;
  while (fscanf(f, "%lu,%lu", &ir, &red) == 2) {
    p.addSample((uint32_t)ir);
    if (++n % (int)FS == 0 && n > 10 * FS) {
      reported++;
      if (p.bpm() >= 79 && p.bpm() <= 91) okSeconds++;
    }
  }
  fclose(f);
  TEST_ASSERT_EQUAL(reported, okSeconds);  // после 10 с — каждую секунду в пределах 79–91
  TEST_ASSERT_GREATER_OR_EQUAL_UINT8(70, p.quality());
  TEST_ASSERT_TRUE(p.rmssd() > 5 && p.rmssd() < 80);
}

static void feedNoise(PulseDetector &p, float seconds) {
  for (int i = 0; i < seconds * FS; i++) p.addSample(120000 + (uint32_t)(noise(60) + 60));
}

// Пульс пропал (дышал часто — рука дёргалась), потом поймалось «52»: в покое пульс не падает
// с 80 до 52 за полминуты — такое значение надо подтверждать дольше
static void test_implausible_jump_after_gap_needs_longer_confirmation() {
  PulseDetector p(FS);
  Ppg g;
  g.bpm = 80;
  feed(p, g, 15);
  feedNoise(p, 12);
  TEST_ASSERT_EQUAL_FLOAT(0, p.bpm());
  g.bpm = 52;
  feed(p, g, 8);
  TEST_ASSERT_EQUAL_FLOAT(0, p.bpm());
  feed(p, g, 10);  // держится долго и ровно — значит, настоящий
  TEST_ASSERT_FLOAT_WITHIN(4, 52, p.bpm());
}

static void test_moderate_rise_after_gap_is_accepted_quickly() {
  PulseDetector p(FS);
  Ppg g;
  g.bpm = 80;
  feed(p, g, 15);
  feedNoise(p, 12);
  g.bpm = 104;  // после частого дыхания пульс выше на 30%
  feed(p, g, 8);
  TEST_ASSERT_FLOAT_WITHIN(5, 104, p.bpm());
}

static void test_motion_freezes_pulse_instead_of_locking_on_steps() {
  PulseDetector p(FS);
  Ppg g;
  feed(p, g, 15);
  p.setMotion(true);
  for (int i = 0; i < 8 * FS; i++) {  // шаги 2.5 Гц сильнее пульса
    float t = i / FS;
    p.addSample(g.next() + (uint32_t)(3000 + 3000 * sinf(2 * PI_F * 2.5f * t)));
  }
  TEST_ASSERT_FLOAT_WITHIN(5, 72, p.bpm());
  TEST_ASSERT_LESS_THAN_UINT8(40, p.quality());
}

static void test_pulse_resumes_after_motion() {
  PulseDetector p(FS);
  Ppg g;
  feed(p, g, 15);
  p.setMotion(true);
  feed(p, g, 5);
  p.setMotion(false);
  g.bpm = 110;  // после прыжков пульс выше
  feed(p, g, 12);
  TEST_ASSERT_FLOAT_WITHIN(5, 110, p.bpm());
}

static void test_led_current_change_keeps_pulse() {
  PulseDetector p(FS);
  Ppg g;
  feed(p, g, 15);
  g.dc = 170000;
  g.ac = 420;
  p.notifyGainChange();
  feed(p, g, 10);
  TEST_ASSERT_FLOAT_WITHIN(4, 72, p.bpm());
}

static void test_losing_contact_clears_pulse() {
  PulseDetector p(FS);
  Ppg g;
  feed(p, g, 15);
  for (int i = 0; i < 2 * FS; i++) p.addSample(1200);
  TEST_ASSERT_FALSE(p.contact());
  TEST_ASSERT_EQUAL_FLOAT(0, p.bpm());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_contact_without_skin);
  RUN_TEST(test_measures_resting_pulse);
  RUN_TEST(test_first_value_appears_within_five_seconds);
  RUN_TEST(test_pulse_comes_back_within_six_seconds_after_motion);
  RUN_TEST(test_measures_fast_pulse_after_running);
  RUN_TEST(test_measures_slow_pulse);
  RUN_TEST(test_weak_wrist_signal);
  RUN_TEST(test_strong_dicrotic_wave_is_not_counted_twice);
  RUN_TEST(test_noise_without_pulse_is_not_reported);
  RUN_TEST(test_tracks_rising_pulse);
  RUN_TEST(test_counts_individual_beats);
  RUN_TEST(test_rmssd_reflects_beat_to_beat_variability);
  RUN_TEST(test_rmssd_is_low_for_steady_rhythm);
  RUN_TEST(test_rmssd_stays_realistic_on_noisy_weak_signal);
  RUN_TEST(test_led_current_change_does_not_inflate_rmssd);
  RUN_TEST(test_loose_strap_artifacts_are_not_taken_for_pulse);
  RUN_TEST(test_pressure_artifacts_in_pulse_range_are_rejected);
  RUN_TEST(test_real_loose_strap_recording);
  RUN_TEST(test_real_wrist_recording);
  RUN_TEST(test_implausible_jump_after_gap_needs_longer_confirmation);
  RUN_TEST(test_moderate_rise_after_gap_is_accepted_quickly);
  RUN_TEST(test_motion_freezes_pulse_instead_of_locking_on_steps);
  RUN_TEST(test_pulse_resumes_after_motion);
  RUN_TEST(test_led_current_change_keeps_pulse);
  RUN_TEST(test_losing_contact_clears_pulse);
  return UNITY_END();
}
