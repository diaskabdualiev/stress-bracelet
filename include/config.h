#pragma once
// Все настройки железа — здесь (общие для прошивки браслета и теста железа).

// ---- Выходы ----
#define PIN_VIBRO          28      // вибромодуль (IN), PWM
#define PIN_LEDS           3       // DIN адресных светодиодов WS2812B / SK6812
#define LED_COUNT          8       // сколько светодиодов подключено (если меньше — не страшно)
#define LED_BRIGHTNESS     3       // 1..255; ярче — режет глаза, ниже 3 жёлтый путается с зелёным

// ---- I2C: OLED + MPU + MAX30102 на одной шине ----
#define I2C_SDA_PIN        0       // на плате модули висят на GP0/GP1
#define I2C_SCL_PIN        1
#define I2C_AUTODETECT     1       // (только тест железа) перебрать другие пары пинов
#define I2C_FREQ           400000

// ---- OLED 0.91" ----
#define OLED_WIDTH         128
#define OLED_HEIGHT        32
#define OLED_ROTATION      1       // браслет: экран вертикально; 3 — перевернуть на 180°

// ---- Тест железа ----
#define PAGE_INTERVAL_MS   4000    // автолистание страниц на OLED
#define FINGER_IR_MIN      50000   // IR выше этого — палец лежит на MAX30102
#define SHAKE_G            0.8f    // встряхивание: |a| отличается от 1g больше чем на столько
#define VIBRO_BEAT_MS      40      // «тик» мотора на каждый удар пульса (0 — выключить)
#define VIBRO_BEAT_PWM     180     // сила «тика», 0..255

// ---- Браслет ----
#define HR_NORM            80      // общая норма пульса в покое (середина нормы 60–100 уд/мин)
#define SKIN_CONTACT_IR    15000   // ИК-сигнал выше — датчик прижат к коже
#define IR_LED_START       0x40    // ток ИК-светодиода MAX30102 при старте (0.2 мА на единицу)
#define IR_LED_MIN         0x10
#define IR_LED_MAX         0xC0
#define RED_LED_CURRENT    0x18    // красный нужен только для записи сигнала
#define BODY_TEMP_NORM     36.6f   // когда температура кожи устоялась, считаем, что тело в норме
#define EXERCISE_DELAY_MS  5000    // после вибрации столько показываем состояние, потом упражнение
#define EXERCISE_CYCLES    4       // кругов «4-4-4» (вдох-задержка-выдох по 4 с)
