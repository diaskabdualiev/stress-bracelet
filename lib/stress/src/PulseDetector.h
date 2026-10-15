#pragma once
#include <cstdint>

#include "Filters.h"

namespace stress {

// Пульс по ИК-каналу MAX30102 с запястья (красный/ИК свет вместо зелёного).
//
// Как добиваемся честных цифр при слабом сигнале:
//   1. полосовой фильтр 0.65–3.5 Гц — оставляем только пульс, убираем дыхательный дрейф;
//   2. пульс считаем двумя способами: автокорреляцией окна 8 с и по отдельным ударам;
//      принимаем значение, только если сигнал действительно периодичен и способы согласны;
//   3. при движении руки и плохом контакте цифры не выдумываем: держим последнее
//      хорошее значение, потом показываем «нет данных» (0);
//   4. сглаживание без скачков — не больше 4 уд/мин за секунду.
class PulseDetector {
 public:
  explicit PulseDetector(float fs = 100.0f);
  void reset();

  bool addSample(uint32_t raw);  // true — на этом отсчёте найден удар сердца
  void notifyGainChange();       // поменяли ток светодиода — дать фильтрам успокоиться
  void setMotion(bool moving);   // рука двигается — сигнал ненадёжен
  void setContactThreshold(float level) { contactLevel_ = level; }

  bool contact() const { return contact_; }
  float bpm() const { return bpm_; }  // 0 — пульс неизвестен
  uint8_t quality() const;            // 0..100
  float rmssd() const;                // вариабельность ритма, мс; 0 — мало данных
  float dc() const { return dc_; }
  float signal() const { return y1_; }  // отфильтрованный сигнал (для графика)

 private:
  static const int ACF_N = 400;  // окно автокорреляции: 8 с при 50 Гц
  static const int IBI_N = 8, DIFF_N = 32;

  void clear();
  void settle();
  void restartWindow();
  bool detectBeat(float y);
  void addIbi(float ms);
  float medianIbi() const;
  float beatsBpm() const;
  void estimate();
  float analyze(int len, float &bpm);
  int64_t acf(int lag, int len) const;

  float fs_, fsDec_, aDc_, aLevel_, envDecay_;
  float contactLevel_ = 15000;
  Biquad hp1_, hp2_, lp_;
  bool started_ = false, contact_ = false, motion_ = false;
  float dc_ = 0, level_ = 0;
  uint32_t n_ = 0, settleUntil_ = 0, fastUntil_ = 0, lastGood_ = 0;

  float win_[ACF_N];
  int16_t work_[ACF_N];
  int winHead_ = 0, winCount_ = 0, decN_ = 0, sinceEst_ = 0;
  float decAcc_ = 0;

  float y1_ = 0, y2_ = 0, env_ = 0;
  double lastBeatT_ = -1;
  float ibis_[IBI_N], diffs_[DIFF_N];
  int ibiCount_ = 0, ibiHead_ = 0, diffCount_ = 0, diffHead_ = 0, rejects_ = 0;
  float lastIbi_ = 0;
  bool lastIbiValid_ = false;

  float bpm_ = 0, candidate_ = 0, quality_ = 0;
};

}  // namespace stress
