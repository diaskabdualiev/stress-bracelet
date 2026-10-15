#include <unity.h>

#include "Breathing.h"

using namespace stress;

void setUp() {}
void tearDown() {}

static void test_inactive_until_started() {
  Breathing b;
  TEST_ASSERT_FALSE(b.active());
}

static void test_phases_follow_inhale_hold_exhale() {
  Breathing b(4, 4000);
  b.start(1000);
  b.update(1000);
  TEST_ASSERT_EQUAL(BreathPhase::Inhale, b.phase());
  b.update(5000);
  TEST_ASSERT_EQUAL(BreathPhase::Hold, b.phase());
  b.update(9000);
  TEST_ASSERT_EQUAL(BreathPhase::Exhale, b.phase());
  b.update(13000);
  TEST_ASSERT_EQUAL(BreathPhase::Inhale, b.phase());
  TEST_ASSERT_EQUAL_UINT8(2, b.cycle());
}

static void test_seconds_left_counts_down_from_four() {
  Breathing b(4, 4000);
  b.start(0);
  b.update(0);
  TEST_ASSERT_EQUAL_UINT8(4, b.secondsLeft());
  b.update(999);
  TEST_ASSERT_EQUAL_UINT8(4, b.secondsLeft());
  b.update(1000);
  TEST_ASSERT_EQUAL_UINT8(3, b.secondsLeft());
  b.update(3999);
  TEST_ASSERT_EQUAL_UINT8(1, b.secondsLeft());
}

static void test_lungs_grow_on_inhale_stay_on_hold_shrink_on_exhale() {
  Breathing b(4, 4000);
  b.start(0);
  b.update(0);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, b.lungs());
  b.update(2000);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, b.lungs());
  b.update(6000);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, b.lungs());
  b.update(10000);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, b.lungs());
}

static void test_update_reports_each_phase_change_once() {
  Breathing b(4, 4000);
  b.start(0);
  TEST_ASSERT_TRUE(b.update(0));  // начало вдоха — тоже смена фазы (для вибро-подсказки)
  TEST_ASSERT_FALSE(b.update(10));
  TEST_ASSERT_TRUE(b.update(4000));
  TEST_ASSERT_FALSE(b.update(4001));
}

static void test_finishes_after_all_cycles() {
  Breathing b(2, 4000);
  b.start(0);
  b.update(0);
  b.update(23999);
  TEST_ASSERT_TRUE(b.active());
  TEST_ASSERT_EQUAL(BreathPhase::Exhale, b.phase());
  TEST_ASSERT_TRUE(b.update(24000));
  TEST_ASSERT_EQUAL(BreathPhase::Done, b.phase());
  TEST_ASSERT_FALSE(b.active());
}

static void test_stop_cancels_exercise() {
  Breathing b;
  b.start(0);
  b.stop();
  TEST_ASSERT_FALSE(b.active());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_inactive_until_started);
  RUN_TEST(test_phases_follow_inhale_hold_exhale);
  RUN_TEST(test_seconds_left_counts_down_from_four);
  RUN_TEST(test_lungs_grow_on_inhale_stay_on_hold_shrink_on_exhale);
  RUN_TEST(test_update_reports_each_phase_change_once);
  RUN_TEST(test_finishes_after_all_cycles);
  RUN_TEST(test_stop_cancels_exercise);
  return UNITY_END();
}
