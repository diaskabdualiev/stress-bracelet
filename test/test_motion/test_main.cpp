#include <unity.h>

#include <cmath>

#include "MotionTracker.h"

using namespace stress;

static const float FS = 50.0f;
static const float PI_F = 3.14159265f;
static uint32_t rng = 12345;

static float noise(float amp) {  // детерминированный шум [-amp, amp]
  rng = rng * 1103515245u + 12345u;
  return ((rng >> 8) / 16777216.0f * 2.0f - 1.0f) * amp;
}

// Рука лежит: гравитация по z + шум датчика
static void feedStill(MotionTracker &m, float seconds) {
  for (int i = 0; i < seconds * FS; i++)
    m.addSample(noise(0.004f), noise(0.004f), 1.0f + noise(0.004f), noise(0.5f), noise(0.5f), noise(0.5f));
}

// Бег/прыжки: 2.5 Гц, ±0.8 g, рука ещё и вращается
static void feedRunning(MotionTracker &m, float seconds) {
  static float t = 0;
  for (int i = 0; i < seconds * FS; i++, t += 1 / FS) {
    float s = sinf(2 * PI_F * 2.5f * t);
    m.addSample(0.3f * s, noise(0.05f), 1.0f + 0.8f * s, 200 * s, noise(5), 80 * s);
  }
}

// Жесты при разговоре: медленные повороты кисти, мало ускорения
static void feedGestures(MotionTracker &m, float seconds) {
  static float t = 0;
  for (int i = 0; i < seconds * FS; i++, t += 1 / FS) {
    float s = sinf(2 * PI_F * 0.7f * t);
    m.addSample(0.04f * s, noise(0.01f), 1.0f + 0.03f * s, 50 * s, 20 * s, noise(3));
  }
}

void setUp() { rng = 12345; }
void tearDown() {}

static void test_resting_hand_is_still() {
  MotionTracker m(FS);
  feedStill(m, 5);
  TEST_ASSERT_EQUAL(Activity::Still, m.activity());
}

static void test_running_is_active_within_three_seconds() {
  MotionTracker m(FS);
  feedStill(m, 3);
  feedRunning(m, 3);
  TEST_ASSERT_EQUAL(Activity::Active, m.activity());
}

static void test_short_bump_is_not_active() {
  MotionTracker m(FS);
  feedStill(m, 3);
  feedRunning(m, 0.6f);  // задел рукой стол
  TEST_ASSERT_NOT_EQUAL(Activity::Active, m.activity());
  feedStill(m, 4);
  TEST_ASSERT_EQUAL(Activity::Still, m.activity());
}

static void test_gestures_are_light_not_active() {
  MotionTracker m(FS);
  feedStill(m, 3);
  feedGestures(m, 6);
  TEST_ASSERT_EQUAL(Activity::Light, m.activity());
}

static void test_stopping_after_running_leaves_active() {
  MotionTracker m(FS);
  feedRunning(m, 10);
  TEST_ASSERT_EQUAL(Activity::Active, m.activity());
  feedStill(m, 8);
  TEST_ASSERT_EQUAL(Activity::Still, m.activity());
}

static void test_gyro_bias_alone_is_still() {
  MotionTracker m(FS);
  for (int i = 0; i < 10 * FS; i++)  // типичный сдвиг нуля гироскопа клона
    m.addSample(noise(0.004f), noise(0.004f), 1.0f + noise(0.004f), 4.0f, -3.0f, 2.5f);
  TEST_ASSERT_EQUAL(Activity::Still, m.activity());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_resting_hand_is_still);
  RUN_TEST(test_running_is_active_within_three_seconds);
  RUN_TEST(test_short_bump_is_not_active);
  RUN_TEST(test_gestures_are_light_not_active);
  RUN_TEST(test_stopping_after_running_leaves_active);
  RUN_TEST(test_gyro_bias_alone_is_still);
  return UNITY_END();
}
