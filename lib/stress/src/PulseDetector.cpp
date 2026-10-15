#include "PulseDetector.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace stress {

static const float MIN_BPM = 50, MAX_BPM = 210;  // пульс ребёнка в покое ниже 50 не бывает
static const float MAX_PULSE_SWING = 0.005f;      // пульс на запястье колеблет сигнал на ~0.02–0.2%; больше 0.5% — датчик «ёрзает»
static const float SQI_MIN = 0.5f;        // насколько сигнал похож на сам себя через период
static const float SQI_LOCK = 0.6f;       // для самого первого значения — построже
static const float SQI_SHORT = 0.7f;      // для короткого окна (4 с) — ещё строже
static const float SQI_TRUST = 0.8f;      // при таком сходстве верим автокорреляции без ударов
static const float PERIODIC_MIN = 0.25f;  // сходство через два периода — отличает пульс от шума
static const float HOLD_S = 6, HOLD_MOTION_S = 30;
static const float STEP_BPM = 4, STEP_FAST_BPM = 15;  // уд/мин за секунду
static const float EST_PERIOD_S = 0.5f;                // оценка дважды в секунду — пульс находится быстрее
static const int ACF_MIN = 150;                        // 3 с при 50 Гц — минимум для оценки
static const float HRV_QUALITY = 70;      // ниже — вариабельность ритма не считаем

PulseDetector::PulseDetector(float fs)
    : fs_(fs),
      fsDec_(fs / 2),
      aDc_(1.0f / (1.5f * fs)),
      aLevel_(1.0f / (0.25f * fs)),
      envDecay_(powf(0.5f, 1.0f / (2.0f * fs))) {  // огибающая амплитуды вдвое за 2 с
  // ФВЧ 4-го порядка (Баттерворт): дыхание и движение сосудов (0.1–0.4 Гц) на запястье
  // сравнимы с самим пульсом — 2-го порядка не хватает
  hp1_.setHighPass(fs, 0.65f, 0.5412f);
  hp2_.setHighPass(fs, 0.65f, 1.3066f);
  lp_.setLowPass(fs, 3.5f);
  reset();
}

void PulseDetector::reset() {
  started_ = contact_ = motion_ = false;
  n_ = 0;
  clear();
}

void PulseDetector::clear() {
  bpm_ = candidate_ = quality_ = 0;
  ibiCount_ = ibiHead_ = diffCount_ = diffHead_ = rejects_ = 0;
  lastIbiValid_ = false;
  lastBeatT_ = -1;
  env_ = y1_ = y2_ = 0;
  restartWindow();
}

void PulseDetector::restartWindow() {
  winHead_ = winCount_ = decN_ = sinceEst_ = 0;
  decAcc_ = 0;
}

void PulseDetector::settle() {
  settleUntil_ = n_ + (uint32_t)(fs_ / 2);  // 0.5 с на переходный процесс
  restartWindow();
  lastBeatT_ = -1;  // интервал «через паузу» — не настоящий
  lastIbiValid_ = false;
}

void PulseDetector::notifyGainChange() {
  if (contact_) settle();
}

void PulseDetector::setMotion(bool moving) {
  if (motion_ && !moving) {  // движение закончилось: старые данные испорчены
    restartWindow();
    ibiCount_ = ibiHead_ = rejects_ = 0;
    lastIbiValid_ = false;
    lastBeatT_ = -1;
    fastUntil_ = n_ + (uint32_t)(12 * fs_);  // пульс после нагрузки мог сильно измениться
  }
  motion_ = moving;
}

bool PulseDetector::addSample(uint32_t raw) {
  n_++;
  float x = (float)raw;
  if (!started_) {
    dc_ = level_ = x;
    started_ = true;
  }
  level_ += (x - level_) * aLevel_;

  // Контакт с кожей: кожа отражает много ИК-света, воздух — почти ничего
  bool c = contact_ ? level_ > 0.8f * contactLevel_ : level_ > contactLevel_;
  if (c != contact_) {
    contact_ = c;
    clear();
    if (contact_) settle();
  }
  if (!contact_) return false;

  if (n_ < settleUntil_) {  // фильтры «успокаиваются»: постоянную составляющую ловим сразу
    dc_ = x;
    hp1_.reset(0);
    hp2_.reset(0);
    lp_.reset(0);
    return false;
  }
  dc_ += (x - dc_) * aDc_;
  float y = -lp_.process(hp2_.process(hp1_.process(x - dc_)));  // минус: систола (провал) → пик

  decAcc_ += y;
  if (++decN_ == 2) {  // 100 → 50 Гц для автокорреляции
    win_[winHead_] = decAcc_ / 2;
    winHead_ = (winHead_ + 1) % ACF_N;
    if (winCount_ < ACF_N) winCount_++;
    decAcc_ = 0;
    decN_ = 0;
    if (++sinceEst_ >= (int)(fsDec_ * EST_PERIOD_S)) {
      sinceEst_ = 0;
      estimate();
    }
  }
  return detectBeat(y);
}

// ---------------------------------------------------------------- отдельные удары

bool PulseDetector::detectBeat(float y) {
  env_ = std::max(env_ * envDecay_, fabsf(y));
  bool beat = false;
  if (y1_ > y2_ && y1_ >= y && y1_ > 0.45f * env_) {
    float den = y2_ - 2 * y1_ + y;
    float off = den != 0 ? std::min(0.5f, std::max(-0.5f, 0.5f * (y2_ - y) / den)) : 0;
    double tPeak = (double)(n_ - 1) + off;
    // Рефрактерный период: дикротическая волна идёт через ~0.3 периода — не считаем её ударом
    double refractory = 0.3 * fs_;
    if (bpm_ > 0) refractory = 0.6 * 60.0 * fs_ / bpm_;
    else if (ibiCount_ >= 3) refractory = 0.6 * medianIbi() * fs_ / 1000.0;
    if (lastBeatT_ < 0 || tPeak - lastBeatT_ >= refractory) {
      if (lastBeatT_ >= 0 && !motion_) addIbi((float)((tPeak - lastBeatT_) * 1000.0 / fs_));
      lastBeatT_ = tPeak;
      beat = !motion_;
    }
  }
  y2_ = y1_;
  y1_ = y;
  return beat;
}

void PulseDetector::addIbi(float ms) {
  if (ms < 60000 / MAX_BPM || ms > 60000 / MIN_BPM) {
    lastIbiValid_ = false;
    return;
  }
  if (ibiCount_ >= 3) {
    float med = medianIbi();
    if (fabsf(ms - med) > 0.25f * med) {  // пропущенный или лишний удар
      lastIbiValid_ = false;
      if (++rejects_ >= 4) ibiCount_ = ibiHead_ = rejects_ = 0;  // ритм реально сменился
      return;
    }
  }
  rejects_ = 0;
  ibis_[ibiHead_] = ms;
  ibiHead_ = (ibiHead_ + 1) % IBI_N;
  if (ibiCount_ < IBI_N) ibiCount_++;
  // ВСР — только по хорошему сигналу: на слабом шум сдвигает моменты ударов на десятки мс.
  // Соседние интервалы в покое отличаются на десятки мс; скачок больше 20% — это сбой детектора.
  if (lastIbiValid_ && quality_ >= HRV_QUALITY && fabsf(ms - lastIbi_) <= 0.2f * medianIbi()) {
    diffs_[diffHead_] = ms - lastIbi_;
    diffHead_ = (diffHead_ + 1) % DIFF_N;
    if (diffCount_ < DIFF_N) diffCount_++;
  }
  lastIbi_ = ms;
  lastIbiValid_ = true;
}

float PulseDetector::medianIbi() const {
  if (!ibiCount_) return 0;
  float tmp[IBI_N];
  memcpy(tmp, ibis_, sizeof(float) * ibiCount_);
  std::sort(tmp, tmp + ibiCount_);
  return tmp[ibiCount_ / 2];
}

float PulseDetector::beatsBpm() const {
  if (ibiCount_ < 5) return 0;
  float lo = *std::min_element(ibis_, ibis_ + ibiCount_);
  float hi = *std::max_element(ibis_, ibis_ + ibiCount_);
  if (hi > 1.3f * lo) return 0;  // разброс большой — удары ненадёжны
  return 60000.0f / medianIbi();
}

// RMSSD через медиану |разностей| (1.4826·медиана ≈ СКО): редкие сбои не раздувают результат
float PulseDetector::rmssd() const {
  if (diffCount_ < 8) return 0;
  float a[DIFF_N];
  for (int i = 0; i < diffCount_; i++) a[i] = fabsf(diffs_[i]);
  std::sort(a, a + diffCount_);
  return 1.4826f * a[diffCount_ / 2];
}

uint8_t PulseDetector::quality() const { return (uint8_t)std::min(100.0f, std::max(0.0f, quality_)); }

// ---------------------------------------------------------------- автокорреляция

int64_t PulseDetector::acf(int lag, int len) const {
  int64_t s = 0;
  for (int i = lag; i < len; i++) s += (int32_t)work_[i] * work_[i - lag];
  return s;
}

// Автокорреляция последних len отсчётов окна. Возвращает «сходство через период» 0..1.
float PulseDetector::analyze(int len, float &bpmAcf) {
  bpmAcf = 0;
  float mean = 0, peak = 0, power = 0;
  int start = (winHead_ - len + ACF_N) % ACF_N;
  for (int i = 0; i < len; i++) mean += win_[(start + i) % ACF_N];
  mean /= len;
  for (int i = 0; i < len; i++) {
    float v = win_[(start + i) % ACF_N] - mean;
    peak = std::max(peak, fabsf(v));
    power += v * v;
  }
  if (peak <= 0) return 0;
  if (sqrtf(power / len) > MAX_PULSE_SWING * dc_) return 0;  // так сильно колеблется не кровь, а датчик
  float scale = 2000.0f / peak;  // в int16, чтобы считать быстро и на RP2040 без FPU
  for (int i = 0; i < len; i++) work_[i] = (int16_t)lrintf((win_[(start + i) % ACF_N] - mean) * scale);

  float r0 = (float)acf(0, len);
  int kMin = (int)(fsDec_ * 60 / MAX_BPM), kMax = (int)ceilf(fsDec_ * 60 / MIN_BPM);
  float best = -2, prev = (float)acf(kMin - 1, len) / r0, cur = (float)acf(kMin, len) / r0, rPrev = 0, rNext = 0;
  int bestK = -1;
  for (int k = kMin; k <= kMax; k++) {
    float next = (float)acf(k + 1, len) / r0;
    if (cur >= prev && cur >= next && cur > best) {
      best = cur;
      bestK = k;
      rPrev = prev;
      rNext = next;
    }
    prev = cur;
    cur = next;
  }
  if (bestK <= 0) return 0;

  float den = rPrev - 2 * best + rNext;
  float lag = bestK + (den != 0 ? 0.5f * (rPrev - rNext) / den : 0);
  bpmAcf = 60 * fsDec_ / lag;
  float sqi = best * len / (len - bestK);  // поправка на укорочение перекрытия
  // Настоящий пульс повторяется и через два периода, узкополосный шум — нет
  int k2 = (int)lrintf(2 * lag);
  if (k2 + 1 < len) {
    float r2 = std::max({acf(k2 - 1, len), acf(k2, len), acf(k2 + 1, len)}) / r0 * len / (len - k2);
    if (r2 < PERIODIC_MIN) sqi *= 0.5f;
  }
  return std::min(1.0f, std::max(0.0f, sqi));
}

void PulseDetector::estimate() {
  if (winCount_ < ACF_MIN) return;  // меньше 3 с — рано
  float bpmAcf, sqi = analyze(winCount_, bpmAcf);
  // Окно 8 с ещё помнит помеху (рука дёрнулась) — пробуем последние 3 с, но строже.
  // Так пульс находится через ~4 с после помехи, а не через 10.
  if (sqi < SQI_MIN && winCount_ > ACF_MIN) {
    float shortBpm, shortSqi = analyze(ACF_MIN, shortBpm);
    if (shortSqi >= SQI_SHORT) {
      sqi = shortSqi;
      bpmAcf = shortBpm;
    }
  }

  quality_ = motion_ ? quality_ * 0.5f : 0.5f * quality_ + 50.0f * sqi;

  bool ok = !motion_ && sqi >= SQI_MIN;
  float bb = beatsBpm();
  bool agree = bb > 0 && fabsf(bb - bpmAcf) <= 0.15f * bpmAcf;
  if (ok && bb > 0 && !agree && sqi < SQI_TRUST) ok = false;
  if (ok && bpm_ <= 0 && sqi < SQI_LOCK) ok = false;

  if (ok) {
    if (bpm_ <= 0) {  // первое значение — только после двух согласных оценок подряд
      if (candidate_ > 0 && fabsf(bpmAcf - candidate_) < 0.1f * candidate_) bpm_ = 0.5f * (bpmAcf + candidate_);
      candidate_ = bpmAcf;
    } else {
      float maxStep = (n_ < fastUntil_ ? STEP_FAST_BPM : STEP_BPM) * EST_PERIOD_S;
      bpm_ += std::min(maxStep, std::max(-maxStep, 0.35f * (bpmAcf - bpm_)));
    }
    lastGood_ = n_;
  } else {
    if (!motion_) candidate_ = 0;
    float hold = motion_ ? HOLD_MOTION_S : HOLD_S;
    if (bpm_ > 0 && n_ - lastGood_ > hold * fs_) bpm_ = 0;
  }
}

}  // namespace stress
