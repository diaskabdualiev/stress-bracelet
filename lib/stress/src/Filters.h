#pragma once
#include <cmath>

namespace stress {

// Биквадратный фильтр 2-го порядка (формулы RBJ Audio EQ Cookbook).
class Biquad {
 public:
  void setLowPass(float fs, float f0, float q = 0.7071f) { set(fs, f0, q, false); }
  void setHighPass(float fs, float f0, float q = 0.7071f) { set(fs, f0, q, true); }

  float process(float x) {
    float y = b0_ * x + z1_;
    z1_ = b1_ * x - a1_ * y + z2_;
    z2_ = b2_ * x - a2_ * y;
    return y;
  }

  // Состояние «как будто на входе давно стоит x» — без переходного процесса.
  void reset(float x = 0) {
    float y = x * (b0_ + b1_ + b2_) / (1 + a1_ + a2_);
    z2_ = b2_ * x - a2_ * y;
    z1_ = y - b0_ * x;
  }

 private:
  void set(float fs, float f0, float q, bool high) {
    float w = 2 * 3.14159265f * f0 / fs, c = cosf(w), alpha = sinf(w) / (2 * q), a0 = 1 + alpha;
    float k = high ? (1 + c) / 2 : (1 - c) / 2;
    b0_ = k / a0;
    b1_ = (high ? -(1 + c) : (1 - c)) / a0;
    b2_ = k / a0;
    a1_ = -2 * c / a0;
    a2_ = (1 - alpha) / a0;
  }
  float b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, z1_ = 0, z2_ = 0;
};

}  // namespace stress
