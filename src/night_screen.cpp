#include "night_screen.h"
#include "globals.h"
#include "fonts.h"
#include "icons.h"
#include "audio_engine.h"
#include "utils.h"
#include <math.h>

// ============================================================
// НОЧНОЙ/УТРЕННИЙ РЕЖИМ (неблокирующий, без delay()).
//
// При входе в ночной режим: короткая анимация моргания глаз (~1.5 сек),
// звук "спокойной ночи", затем чёрный экран с часами, градусником и
// уличной температурой. Фоновой картинки больше нет.
//
// Утром: по умолчанию без анимации глаз, просто звук "доброе утро" и
// возврат к обычным экранам. Чтобы вернуть моргание при пробуждении,
// поставьте NIGHT_WAKE_ANIMATION = true.
// ============================================================

// Яркость ночного экрана в процентах (10..100). Определена здесь, объявлена в night_screen.h
uint8_t nightScreenBrightnessPercent = 100;

// Приглушает цвет RGB565: каждый канал умножается на pct/100. При 100% цвет не меняется.
static uint16_t dimColor(uint16_t c, uint8_t pct) {
  if (pct >= 100) return c;
  uint32_t r = ((c >> 11) & 0x1F) * 255 / 31;
  uint32_t g = ((c >> 5) & 0x3F) * 255 / 63;
  uint32_t b = (c & 0x1F) * 255 / 31;
  r = r * pct / 100;
  g = g * pct / 100;
  b = b * pct / 100;
  return display->color565((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

enum class NightPhase { IDLE_OFF, CLOSING, SLEEPING, WAKING };
static NightPhase phase = NightPhase::IDLE_OFF;

struct FrameStep { int frameIndex; unsigned long durationMs; };

// Индекс 0..7 соответствует eyes, eyes1..eyes7. Общая длительность ~1.5 сек.
static const FrameStep closingSeq[] = {
  {0, 300}, {1, 200}, {2, 300}, {3, 300}, {4, 200}, {5, 100}, {6, 100}, {7, 10}
};
static const FrameStep wakingSeq[] = {
  {0, 300}, {1, 200}, {2, 300}, {3, 300}, {4, 200}, {5, 100}, {6, 100}, {7, 10},
  {0, 10},
  {1, 300}, {2, 300}, {3, 300}, {4, 200}, {5, 100}, {6, 100}, {7, 100},
  {0, 500}
};

static const uint16_t* eyeFrame(int i) {
  switch (i) {
    case 0: return image_data_eyes;
    case 1: return image_data_eyes1;
    case 2: return image_data_eyes2;
    case 3: return image_data_eyes3;
    case 4: return image_data_eyes4;
    case 5: return image_data_eyes5;
    case 6: return image_data_eyes6;
    default: return image_data_eyes7;
  }
}

static int stepIndex = 0;
static unsigned long stepStartedAt = 0;
static bool soundPlayed = false;

// ---------- РАСКЛАДКА НОЧНОГО ЭКРАНА (64x32) ----------
static const int NIGHT_CLOCK_DX = 0;   // сдвиг часов по X (0 = как на основном экране) <-- ПОДВИНУТЬ
static const int NIGHT_THERM_X = 50;   // градусник: левый край (иконка 12x12)       <-- ПОДВИНУТЬ
static const int NIGHT_THERM_Y = 3;    // градусник: верхний край                    <-- ПОДВИНУТЬ
static const int NIGHT_LABEL_X = 7;    // подпись "улица": левый край                <-- ПОДВИНУТЬ
static const int NIGHT_LABEL_Y = 29;   // подпись "улица": базовая линия             <-- ПОДВИНУТЬ
static const int NIGHT_TEMP_Y = NIGHT_LABEL_Y; // температура на одной линии с "улица" <-- ПОДВИНУТЬ
static const int NIGHT_TEMP_RIGHT = 63; // правый край группы "число+градус" (63 = у края экрана) <-- ПОДВИНУТЬ

// Моргание глаз при пробуждении утром (false = сразу звук и обычные экраны)
static const bool NIGHT_WAKE_ANIMATION = false;

// Точки дней недели внизу. Выключены, внизу подпись "улица".
static const bool NIGHT_SHOW_WEEKDAY_DOTS = false;

// Уличная температура - ТОЛЬКО с датчика DS18B20. Интернет-погоду сюда
// не подставляем: иначе при обрыве датчика этого не видно. Нет данных -
// на экране прочерк "--".
static bool getOutdoorTemp(float &t) {
  if (!outdoorTempValid) return false;
  t = outdoorTemp;
  return true;
}

void checkNightSchedule() {
  if (!nightModeScheduleEnabled) return;

  bool insideWindow;
  if (nightOnHour == nightOffHour && nightOnMinute == nightOffMinute) {
    insideWindow = false; // окно нулевой длины - считаем выключенным
  } else if (nightOnHour < nightOffHour ||
             (nightOnHour == nightOffHour && nightOnMinute < nightOffMinute)) {
    // окно НЕ через полночь, напр. 13:00..18:00
    bool afterOn = (hours > nightOnHour) || (hours == nightOnHour && minutes >= nightOnMinute);
    bool beforeOff = (hours < nightOffHour) || (hours == nightOffHour && minutes < nightOffMinute);
    insideWindow = afterOn && beforeOff;
  } else {
    // окно ЧЕРЕЗ полночь, напр. 23:00..7:00
    bool afterOn = (hours > nightOnHour) || (hours == nightOnHour && minutes >= nightOnMinute);
    bool beforeOff = (hours < nightOffHour) || (hours == nightOffHour && minutes < nightOffMinute);
    insideWindow = afterOn || beforeOff;
  }

  if (insideWindow && phase == NightPhase::IDLE_OFF) {
    // Наступила ночь - запускаем анимацию засыпания
    phase = NightPhase::CLOSING;
    stepIndex = 0;
    stepStartedAt = millis();
    soundPlayed = false;
  } else if (!insideWindow && (phase == NightPhase::SLEEPING || phase == NightPhase::CLOSING)) {
    // Наступило утро
    if (NIGHT_WAKE_ANIMATION) {
      phase = NightPhase::WAKING;
      stepIndex = 0;
      stepStartedAt = millis();
      soundPlayed = false;
    } else {
      playAudioFile(morningSoundPath); // "доброе утро"
      phase = NightPhase::IDLE_OFF;
    }
  }
}

bool isNightModeActive() {
  return phase != NightPhase::IDLE_OFF;
}

// Рисует два ряда точек-маркеров дня недели, как в оригинале Nreg.ino
static void drawWeekdayDots() {
  const int dotX[7] = {2, 11, 20, 29, 38, 47, 56};

  for (int day = 0; day < 7; day++) {
    if (day == wdayNum) {
      display->drawRGBBitmap(dotX[day], 25, image_data_BLUE, 5, 3);
    }
    bool isWeekend = (day == 0 || day == 6);
    if (alarmEnabled[day]) {
      display->drawRGBBitmap(dotX[day], 28, isWeekend ? image_data_RED : image_data_GREEN, 5, 2);
    } else {
      display->drawRGBBitmap(dotX[day], 28, image_data_GREY, 5, 2);
    }
  }
}

void drawNightScreen() {
  unsigned long now = millis();

  if (phase == NightPhase::CLOSING) {
    if (!soundPlayed) {
      playAudioFile(nightSoundPath); // "спокойной ночи"
      soundPlayed = true;
    }
    const FrameStep &step = closingSeq[stepIndex];
    display->drawRGBBitmap(0, 0, eyeFrame(step.frameIndex), 64, 32);
    if (now - stepStartedAt >= step.durationMs) {
      stepIndex++;
      stepStartedAt = now;
      if (stepIndex >= (int)(sizeof(closingSeq) / sizeof(closingSeq[0]))) {
        phase = NightPhase::SLEEPING; // засыпание закончилось - спим
      }
    }
    return;
  }

  if (phase == NightPhase::SLEEPING) {
    display->fillScreen(0); // чёрный фон, без картинки

    // Яркость ночного экрана: все цвета ниже приглушаются на этот процент
    uint8_t pct = nightScreenBrightnessPercent;
    if (pct < 10) pct = 10;
    if (pct > 100) pct = 100;
    uint16_t clockCol = dimColor(nightClockColor, pct);
    uint16_t tempCol = dimColor(nightTempColor, pct);
    uint16_t labelCol = dimColor(nightLabelColor, pct);

    // --- Часы слева (шрифт и цвет из настроек "Ночь/утро"), двоеточие не мигает
    int idx = nightFontIndex;
    if (idx < 0 || idx >= CLOCK_FONT_COUNT) idx = 0;
    display->setFont(clockFontVariants[idx]);
    display->setTextSize(1);
    display->setTextColor(clockCol);
    char buf[3];
    sprintf(buf, "%02d", hours);
    display->setCursor(1 + NIGHT_CLOCK_DX, 15 + SHIFT_Y);
    display->print(buf);
    display->setCursor(22 + NIGHT_CLOCK_DX, 12 + SHIFT_Y);
    display->print(":");
    sprintf(buf, "%02d", minutes);
    display->setCursor(27 + NIGHT_CLOCK_DX, 15 + SHIFT_Y);
    display->print(buf);

    // --- Справа вверху: градусник; уличная температура внизу на одной линии с "улица"
    if (pct >= 100) {
      display->drawRGBBitmap(NIGHT_THERM_X, NIGHT_THERM_Y, image_data_Image22, 12, 12);
    } else {
      static uint16_t thermDim[12 * 12];   // градусник с приглушёнными цветами
      for (int i = 0; i < 12 * 12; i++) thermDim[i] = dimColor(image_data_Image22[i], pct);
      display->drawRGBBitmap(NIGHT_THERM_X, NIGHT_THERM_Y, thermDim, 12, 12);
    }
    display->setFont(&TomThumb);
    display->setTextColor(tempCol);   // свой цвет температуры
    char tbuf[8];
    float outT;
    bool outOk = getOutdoorTemp(outT);
    // Нет уличного датчика - показываем температуру из погоды (сервер/провайдер),
    // чтобы на ночных часах не висело "--", когда данные о погоде уже есть.
    if (!outOk && weatherSourceUsed.length() > 0) {
      outT = temperaturedarc;
      outOk = true;
    }
    if (outOk) {
      snprintf(tbuf, sizeof(tbuf), "%+d", (int)round(outT));
    } else {
      snprintf(tbuf, sizeof(tbuf), "--");
    }
    // Число рисуется шрифтом, значок градуса - точками. Реальную ширину числа
    // меряем getTextBounds (этот TomThumb шире, чем 4 px на символ), группа
    // "число+градус" прижата к правому краю (NIGHT_TEMP_RIGHT).
    int16_t bx, by;
    uint16_t bw, bh;
    display->getTextBounds(tbuf, 0, NIGHT_TEMP_Y, &bx, &by, &bw, &bh);
    int tx = NIGHT_TEMP_RIGHT - 3 - bx - (int)bw;  // 1 px зазор + градус 3 px
    if (tx < 0) tx = 0;
    display->setTextWrap(false);
    display->setCursor(tx, NIGHT_TEMP_Y);
    display->print(tbuf);
    display->setTextWrap(true);
    if (outOk) {                            // значок градуса - только когда есть значение
      int dx = tx + bx + (int)bw + 1;       // справа от числа
      int dy = NIGHT_TEMP_Y - 5;            // вровень с верхом цифр
      display->drawPixel(dx + 1, dy,     tempCol);
      display->drawPixel(dx,     dy + 1, tempCol);
      display->drawPixel(dx + 2, dy + 1, tempCol);
      display->drawPixel(dx + 1, dy + 2, tempCol);
    }

    // --- Внизу: подпись "улица" (статично)
    display->setFont(&Heebo7pt8b);
    display->setTextColor(labelCol);  // свой цвет подписи
    display->setCursor(NIGHT_LABEL_X, NIGHT_LABEL_Y);
    display->print(utf8rus("улица"));
    display->setFont();

    if (NIGHT_SHOW_WEEKDAY_DOTS) drawWeekdayDots();
    return;
  }

  if (phase == NightPhase::WAKING) {
    const FrameStep &step = wakingSeq[stepIndex];
    display->drawRGBBitmap(0, 0, eyeFrame(step.frameIndex), 64, 32);
    if (now - stepStartedAt >= step.durationMs) {
      stepIndex++;
      stepStartedAt = now;
      if (stepIndex >= (int)(sizeof(wakingSeq) / sizeof(wakingSeq[0]))) {
        if (!soundPlayed) {
          playAudioFile(morningSoundPath); // "доброе утро"
          soundPlayed = true;
        }
        phase = NightPhase::IDLE_OFF; // пробуждение закончилось - выходим
      }
    }
    return;
  }
}
