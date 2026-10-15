#pragma once
#include <Wire.h>

// Минимальный драйвер MAX30102. Свой, а не SparkFun: там внутренний буфер всего на 4 отсчёта,
// и если цикл задержится на 40 мс (обновление экрана, вибро), отсчёты теряются и пульс «плывёт».
// Здесь каждый раз забираем всё, что накопилось в FIFO датчика (до 32 отсчётов = 320 мс).
class Max30102 {
 public:
  static const int FS = 100;  // 400 Гц с усреднением по 4 → 100 отсчётов в секунду

  bool begin(TwoWire &wire, uint8_t addr = 0x57);
  void setLedCurrent(uint8_t red, uint8_t ir);  // 0.2 мА на единицу
  uint8_t irCurrent() const { return ir_; }

  // Читает накопленные отсчёты (не больше max). Возвращает их количество.
  int readFifo(uint32_t *red, uint32_t *ir, int max);

  // Температура кристалла — датчик прижат к коже, поэтому это температура кожи.
  void startTemperature();
  bool readTemperature(float &celsius);  // false — ещё не готово

 private:
  bool write(uint8_t reg, uint8_t value);
  bool read(uint8_t reg, uint8_t *buf, size_t len);

  TwoWire *wire_ = nullptr;
  uint8_t addr_ = 0x57, red_ = 0, ir_ = 0;
};
