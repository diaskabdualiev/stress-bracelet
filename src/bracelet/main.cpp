// Умный браслет «антистресс» для школьника — Waveshare RP2040-Zero.
//
//   MAX30102 (тыльная сторона запястья) — пульс и температура кожи
//   MPU6500 — движение: бег и прыжки поднимают пульс, но это не стресс
//   OLED 0.91" вертикально — пульс, температура, смайлик, светофор
//   WS2812 — цвет светофора (ровно, без миганий), вибромодуль — напоминание и упражнение «4-4-4»
//
// Стресс: пульс выше общей нормы 80 (+10 — жёлтый с 90, +25 — красный со 105) держится 5 секунд,
// рука в покое.
// Тогда вибрация, 5 с показываем состояние и запускаем дыхание: вдох 4 с — задержка 4 с — выдох 4 с.
//
// Кнопка BOOT: запустить упражнение (ещё раз — остановить).
// Serial 115200: h — список команд.

#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>
#include <hardware/adc.h>

#include "Breathing.h"
#include "Display.h"
#include "Feedback.h"
#include "Imu.h"
#include "Max30102.h"
#include "MotionTracker.h"
#include "PulseDetector.h"
#include "StressEngine.h"
#include "config.h"

using namespace stress;

static_assert(I2C_SDA_PIN % 2 == 0 && I2C_SCL_PIN % 2 == 1 &&
                  ((I2C_SDA_PIN >> 1) & 1) == ((I2C_SCL_PIN >> 1) & 1),
              "SDA/SCL должны принадлежать одному контроллеру I2C (например GP0/GP1)");

static TwoWire &bus = ((I2C_SDA_PIN >> 1) & 1) ? Wire1 : Wire;
static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &bus, -1, I2C_FREQ, I2C_FREQ);
static Max30102 ppg;
static Imu imu;
static PulseDetector pulse(Max30102::FS);
static MotionTracker motion(50);
static StressEngine engine;
static Breathing breathing(EXERCISE_CYCLES, 4000);
static Vibro vibro(PIN_VIBRO);
static StatusLed led(LED_COUNT, PIN_LEDS);
static bool oledOk = false, ppgOk = false, imuOk = false;

static uint32_t lastBeatMs = 0, contactSince = 0, pendingExerciseAt = 0, resultUntil = 0;
static int hrBefore = 0, hrAfter = 0;
static bool rawStream = false, logStream = true;
static uint8_t irCurrent = IR_LED_START;

static int bpmNow() { return (int)lroundf(pulse.bpm()); }

// Для итога упражнения: если пульс на миг пропал (рука шевельнулась) — последнее надёжное
// значение не старше 10 с, иначе 0 (на экране будет просто «OK»)
static int lastBpm = 0;
static uint32_t lastBpmMs = 0;
static int recentBpm(uint32_t now) {
  if (bpmNow() > 0) return bpmNow();
  return (lastBpm > 0 && now - lastBpmMs <= 10000) ? lastBpm : 0;
}

// ============================================================== температура
//
// Термометр MAX30102 — датчик на кристалле. Он прижат к коже, но его сильно греет сама плата:
// на столе он показывает ~36 °C, на руке доходит до ~39 °C и долго «ползёт». Поэтому градусы тела
// напрямую не измерить. Показываем оценку: медленное среднее датчика на руке принимаем за 36.6
// («минусуем» подогрев), а быстрые изменения показываем ослабленными — видно, что кожа теплеет
// или остывает, но цифры остаются правдоподобными (36.1…37.1).

static const float TEMP_GAIN = 0.3f;       // доля изменений датчика, которую показываем
static const float TEMP_SWING = 0.5f;      // не дальше чем на столько от 36.6
static const float TEMP_TAU_WARMUP = 120;  // с: среднее быстро догоняет прогрев первые 10 мин
static const float TEMP_TAU = 900;         // с: потом — медленно
static const uint32_t TEMP_SHOW_AFTER_MS = 20000;

static float skinT = NAN, skinRef = NAN;

static float bodyTemp() {
  if (!pulse.contact() || isnan(skinRef) || millis() - contactSince < TEMP_SHOW_AFTER_MS) return NAN;
  return BODY_TEMP_NORM + constrain(TEMP_GAIN * (skinT - skinRef), -TEMP_SWING, TEMP_SWING);
}

static void temperatureUpdate(uint32_t now) {
  static uint32_t next = 0, startedAt = 0;
  static bool pending = false;
  if (!pending && (int32_t)(now - next) >= 0) {
    ppg.startTemperature();
    pending = true;
    startedAt = now;
    next = now + 10000;
  }
  float t;
  if (pending && now - startedAt >= 40 && ppg.readTemperature(t)) {
    pending = false;
    skinT = isnan(skinT) ? t : skinT + (t - skinT) * 0.3f;
    if (pulse.contact()) {
      float tau = (now - contactSince < 600000) ? TEMP_TAU_WARMUP : TEMP_TAU;
      skinRef = isnan(skinRef) ? skinT : skinRef + (skinT - skinRef) * (10.0f / tau);
    }
  }
  if (!pulse.contact()) skinRef = NAN;
}

// ============================================================== датчики

static void ppgUpdate(uint32_t now) {
  uint32_t red[32], ir[32];
  int n = ppg.readFifo(red, ir, 32);
  for (int i = 0; i < n; i++) {
    bool had = pulse.contact();
    if (pulse.addSample(ir[i])) lastBeatMs = now;
    if (!had && pulse.contact()) contactSince = now;
    if (rawStream) Serial.printf("%lu,%lu\n", (unsigned long)ir[i], (unsigned long)red[i]);
  }
}

static void imuUpdate() {
  float ax, ay, az, gx, gy, gz;
  if (!imu.read(ax, ay, az, gx, gy, gz)) return;
  if (!vibro.busy()) motion.addSample(ax, ay, az, gx, gy, gz);  // свою же вибрацию за движение не считаем
  pulse.setMotion(motion.activity() == Activity::Active || motion.accelRms() > 0.08f);
}

// Подстраиваем ток ИК-светодиода, только если сигнал с кожи вылез за удобный диапазон АЦП (из 262 000):
// каждая смена тока перезапускает поиск пульса, поэтому зря не дёргаем
static void autoGain() {
  uint8_t want = irCurrent;
  if (!pulse.contact()) want = IR_LED_START;
  else if (pulse.dc() > 230000 && irCurrent > IR_LED_MIN) want = max<int>(IR_LED_MIN, irCurrent * 3 / 4);
  else if (pulse.dc() < 30000 && irCurrent < IR_LED_MAX) want = min<int>(IR_LED_MAX, irCurrent * 3 / 2 + 1);
  if (want == irCurrent) return;
  irCurrent = want;
  ppg.setLedCurrent(RED_LED_CURRENT, irCurrent);
  pulse.notifyGainChange();
}

// ============================================================== упражнение 4-4-4

static void startExercise(uint32_t now) {
  pendingExerciseAt = 0;
  resultUntil = 0;
  hrBefore = recentBpm(now);
  breathing.start(now);
}

static void exerciseUpdate(uint32_t now) {
  if (pendingExerciseAt && (int32_t)(now - pendingExerciseAt) >= 0) startExercise(now);
  if (!breathing.active() || !breathing.update(now)) return;
  switch (breathing.phase()) {
    case BreathPhase::Inhale: vibro.play(vibe::inhale); break;
    case BreathPhase::Hold: vibro.play(vibe::hold); break;
    case BreathPhase::Exhale: vibro.play(vibe::exhale); break;
    case BreathPhase::Done:
      vibro.play(vibe::done);
      hrAfter = recentBpm(now);
      resultUntil = now + 6000;
      Serial.printf("Упражнение закончено: пульс %d -> %d\n", hrBefore, hrAfter);
      break;
  }
}

// ============================================================== логика раз в секунду

static const char *stateName(State s) {
  switch (s) {
    case State::NoContact: return "NO_CONTACT";
    case State::Calibrating: return "CALIBRATING";
    case State::Calm: return "CALM";
    case State::Mild: return "MILD";
    case State::Stress: return "STRESS";
    case State::Active: return "ACTIVE";
    case State::Recovery: return "RECOVERY";
  }
  return "?";
}

static const char *activityName(Activity a) {
  return a == Activity::Active ? "ACTIVE" : a == Activity::Light ? "LIGHT" : "STILL";
}

static const char *fmtTemp(char *buf, float t) {  // урезанный printf печатает NAN как «inf»
  if (isnan(t)) return "-";
  snprintf(buf, 8, "%.2f", t);
  return buf;
}

static void printLog() {
  const Diagnostics &d = engine.diagnostics();
  char skin[8], body[8];
  Serial.printf("st=%s bpm=%d q=%u rmssd=%.0f hr~=%.1f base=%.1f d=%+.1f thr=%.0f/%.0f%s act=%s acc=%.3f "
                "skin=%s body=%s ir=%.0f led=0x%02X\n",
                stateName(engine.state()), bpmNow(), pulse.quality(), pulse.rmssd(), d.hr, engine.baseline().bpm,
                d.delta, d.mildThr, d.stressThr, d.declining ? " DOWN" : "", activityName(motion.activity()),
                motion.accelRms(), fmtTemp(skin, skinT), fmtTemp(body, bodyTemp()), pulse.dc(), irCurrent);
}

static void tick(uint32_t now) {
  autoGain();
  if (bpmNow() > 0) {
    lastBpm = bpmNow();
    lastBpmMs = now;
  }
  Inputs in;
  in.ms = now;
  in.bpm = pulse.bpm();
  in.quality = pulse.quality();
  in.rmssd = pulse.rmssd();
  in.activity = motion.activity();
  in.skinTemp = NAN;  // кристалл греет плата — остывание кожи по нему не разглядеть
  in.contact = pulse.contact();
  Event e = engine.update(in);

  switch (e) {
    case Event::BecameMild:
    case Event::BecameStress:
      Serial.printf(">>> %s\n", e == Event::BecameStress ? "СТРЕСС (красный)" : "ВОЛНЕНИЕ (жёлтый)");
      if (breathing.active()) break;  // уже дышим — не мешаем
      if (e == Event::BecameStress) vibro.play(vibe::stress);
      else vibro.play(vibe::mild);
      pendingExerciseAt = now + EXERCISE_DELAY_MS;
      break;
    default: break;
  }
  if (logStream && !rawStream) printLog();
}

// ============================================================== кнопка BOOT

static void buttonUpdate(uint32_t now) {
  static bool was = false;
  bool down = BOOTSEL;
  if (down && !was) {
    vibro.play(vibe::tick);
    if (breathing.active()) breathing.stop();
    else startExercise(now);
  }
  was = down;
}

// ============================================================== вывод

// Светодиод — только светофор (зелёный / жёлтый / красный), ровным светом, без миганий
static void ledUpdate() {
  static int8_t shown = -1;
  int8_t color = -1;  // -1 — не горит (браслет не на руке или идёт калибровка)
  switch (engine.state()) {
    case State::Calm:
    case State::Active:    // бег — не стресс
    case State::Recovery: color = 0; break;
    case State::Mild: color = 1; break;
    case State::Stress: color = 2; break;
    default: break;
  }
  if (color == shown) return;
  shown = color;
  static const uint8_t rgb[3][3] = {{0, 255, 0}, {255, 170, 0}, {255, 0, 0}};
  if (color < 0) led.show(0, 0, 0, 0);
  else led.show(rgb[color][0], rgb[color][1], rgb[color][2], 1);
}

static void displayUpdate(uint32_t now) {
  if (breathing.active()) {
    ui::drawBreathing(oled, breathing, bpmNow());
  } else if ((int32_t)(resultUntil - now) > 0) {
    ui::drawResult(oled, hrBefore, hrAfter);
  } else {
    ui::MainView v;
    v.bpm = bpmNow();
    v.quality = pulse.contact() ? pulse.quality() : 0;
    v.beat = now - lastBeatMs < 150;
    v.temp = bodyTemp();
    v.state = engine.state();
    v.calibration = engine.calibrationProgress();
    v.frame = (now / 350) % 3;
    ui::drawMain(oled, v);
  }
  oled.display();
}

// Проверка вибромотора: 1 с на полную и замер напряжения на пине во время вибрации (пин — вход АЦП)
static void vibroCheck() {
  analogWrite(PIN_VIBRO, 255);
  delay(150);
#if PIN_VIBRO >= 26 && PIN_VIBRO <= 29
  adc_init();
  adc_select_input(PIN_VIBRO - 26);
  uint32_t sum = 0;
  for (int i = 0; i < 64; i++) sum += adc_read();
  float volts = sum * 3.3f / (64 * 4096.0f);
#endif
  delay(850);
  analogWrite(PIN_VIBRO, 0);
#if PIN_VIBRO >= 26 && PIN_VIBRO <= 29
  Serial.printf(">>> Вибро GP%d: на пине %.2f В во время вибрации (норма ~3.3 В)\n", PIN_VIBRO, volts);
#endif
}

static void printHelp() {
  Serial.println("Команды: h — помощь, e — упражнение, v — проверка вибро,");
  Serial.println("         l — вкл/выкл лог, r — сырой сигнал ИК,красный (100 Гц) для записи");
}

static void handleSerial(uint32_t now) {
  while (Serial.available()) {
    int c = Serial.read();
    switch (c) {
      case 'h': printHelp(); break;
      case 'e': if (breathing.active()) breathing.stop(); else startExercise(now); break;
      case 'v': vibroCheck(); break;
      case 'l': logStream = !logStream; break;
      case 'r': rawStream = !rawStream; break;
    }
  }
}

// ============================================================== setup / loop

void setup() {
  vibro.begin();
  led.begin(LED_BRIGHTNESS);
  Serial.begin(115200);
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 1500;) delay(10);
  Serial.println("\n=== Браслет «антистресс» ===");

  bus.setSDA(I2C_SDA_PIN);
  bus.setSCL(I2C_SCL_PIN);
  bus.begin();
  bus.setClock(I2C_FREQ);

  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false) || oled.begin(SSD1306_SWITCHCAPVCC, 0x3D, true, false);
  if (oledOk) {
    oled.setRotation(OLED_ROTATION);
    ui::drawSplash(oled);
    oled.display();
  }
  imuOk = imu.begin(bus);
  ppgOk = ppg.begin(bus);
  if (ppgOk) ppg.setLedCurrent(RED_LED_CURRENT, IR_LED_START);
  pulse.setContactThreshold(SKIN_CONTACT_IR);
  engine.setBaseline({HR_NORM, 0, NAN});  // общая норма для всех; ВСР не сравниваем — она у всех своя

  Serial.printf("OLED: %s, MPU: %s (WHO_AM_I=0x%02X), MAX30102: %s\n", oledOk ? "OK" : "НЕТ", imuOk ? "OK" : "НЕТ",
                imu.whoAmI(), ppgOk ? "OK" : "НЕТ");
  Serial.printf("Норма пульса: %.0f уд/мин — жёлтый с %.0f, красный с %.0f\n", (float)HR_NORM, HR_NORM + 10.0f,
                HR_NORM + 25.0f);
  printHelp();
  vibro.play(vibe::tick);
  delay(800);
}

void loop() {
  static uint32_t tPpg = 0, tImu = 0, tBtn = 0, tTick = 0, tLed = 0, tDisp = 0;
  uint32_t now = millis();

  if (ppgOk && now - tPpg >= 20) {
    tPpg = now;
    ppgUpdate(now);
    temperatureUpdate(now);
  }
  if (imuOk && now - tImu >= 20) {
    tImu = now;
    imuUpdate();
  }
  if (now - tBtn >= 20) {
    tBtn = now;
    buttonUpdate(now);
  }
  if (now - tTick >= 1000) {
    tTick = now;
    tick(now);
  }
  exerciseUpdate(now);
  vibro.update(now);
  if (now - tLed >= 20) {
    tLed = now;
    ledUpdate();
  }
  if (oledOk && now - tDisp >= 40) {
    tDisp = now;
    displayUpdate(now);
  }
  handleSerial(now);
}
