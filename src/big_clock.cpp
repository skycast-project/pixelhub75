#include "big_clock.h"
#include <stdio.h>
#include "globals.h"
#include "fonts.h"

// Длительность анимации выезда (мс). В оригинале Шилова шаги были
// блокирующими (delay() на каждый пиксель) и суммарно занимали от ~650мс
// до ~1900мс в зависимости от направления - у нас всё замеряется через
// millis(), без единого delay(), поэтому длительность одинаковая для
// всех направлений и задаётся явно здесь.
static const unsigned long BIG_CLOCK_ANIM_DURATION_MS = 600;

void activateBigClock() {
  bigClockActive = true;
  // "Хаотично" (4) - каждый раз выбираем случайное направление 0..3,
  // иначе используем то, что выбрано в настройках как есть
  bigClockAnimDir = (bigClockAnimMode == 4) ? random(0, 4) : bigClockAnimMode;
  bigClockAnimPlaying = true;
  bigClockAnimStartMs = millis();
}

void checkBigClockSchedule() {
  if (!bigClockScheduleEnabled) return;

  // Срабатывает один раз в момент совпадения часа/минуты - lastMinute
  // защищает от повторного триггера в течение той же минуты
  static int lastMinute = -1;
  if (minutes == lastMinute) return;
  lastMinute = minutes;

  if (hours == bigClockOnHour && minutes == bigClockOnMinute) {
    activateBigClock();
    Serial.println("🕐 Большие часы включены по расписанию");
  }
  if (hours == bigClockOffHour && minutes == bigClockOffMinute) {
    bigClockActive = false;
    bigClockAnimPlaying = false;
    Serial.println("🕐 Большие часы выключены по расписанию");
  }
}

// ПОДСКАЗКА ДЛЯ РУЧНОЙ ПОДГОНКИ: увеличиваете x/y - элемент едет
// вправо/вниз соответственно (как и в остальных экранах проекта).
// Базовые (конечные) координаты элементов - те же значения, что были
// раньше, когда анимации не было. offsetX/offsetY ниже добавляются к
// ним ТОЛЬКО пока идёт анимация выезда, на финальный кадр они равны 0.
void drawBigClockScreen() {
  int offsetX = 0;
  int offsetY = 0;

  if (bigClockAnimPlaying) {
    unsigned long elapsed = millis() - bigClockAnimStartMs;
    float progress = (float)elapsed / (float)BIG_CLOCK_ANIM_DURATION_MS;
    if (progress >= 1.0f) {
      progress = 1.0f;
      bigClockAnimPlaying = false; // анимация доиграна, дальше рисуем статично
    }
    float remaining = 1.0f - progress; // 1.0 -> старт (за краем экрана), 0.0 -> на месте

    switch (bigClockAnimDir) {
      case 0: // слева
        offsetX = -(int)(remaining * PANEL_W);
        break;
      case 1: // справа
        offsetX = (int)(remaining * PANEL_W);
        break;
      case 2: // сверху
        offsetY = -(int)(remaining * PANEL_H);
        break;
      case 3: // снизу
        offsetY = (int)(remaining * PANEL_H);
        break;
    }
  }

  display->setFont(bigFontVariants[bigFontIndex]);
  display->setTextColor(bigClockColor);

  char hourBuf[3];
  char minBuf[3];
  // Час - без ведущего нуля, как в оригинале ("H", не "HH")
  snprintf(hourBuf, sizeof(hourBuf), "%d", hours);
  // Минуты - С ведущим нулём через %02d. В оригинале здесь был баг
  // (строка "mintext == 0;" - сравнение вместо присваивания "mintext = "0""),
  // ведущий ноль скорее всего не работал по-настоящему - тут это исправлено.
  snprintf(minBuf, sizeof(minBuf), "%02d", minutes);

  // ВАЖНО: ширина цифр в этом шрифте (курсив) РАЗНАЯ - "1" заметно уже,
  // чем "2".."9", и это же справедливо для всех 13 вариантов шрифта
  // (у каждого своя метрика). Раньше здесь были жёстко зашитые координаты
  // (2/28/36), подобранные на глазок под один шрифт - на других шрифтах
  // или при других цифрах текст либо наезжал на двоеточие, либо последняя
  // цифра минут вылезала за правый край матрицы (64 px) и не была видна.
  // Теперь ширина часа/двоеточия/минут измеряется по-настоящему через
  // getTextBounds() перед отрисовкой, и вся конструкция "Ч:ММ" всегда
  // центрируется по фактической ширине - работает одинаково для любого
  // из 13 шрифтов и любых цифр.
  // ВАЖНО: getTextBounds() возвращает не только ширину (hourW/colonW/minW),
  // но и x1 - смещение самого ЛЕВОГО закрашенного пикселя глифа относительно
  // точки, переданной как x=0. Для прямых шрифтов x1 обычно 0 или близко
  // к 0, но для КУРСИВНЫХ/НАКЛОННЫХ (Oblique) шрифтов, которых половина
  // среди 13 вариантов, x1 может быть заметно положительным - и если его
  // игнорировать (как было раньше - одна общая переменная bx на все три
  // вызова, значение не использовалось), реальный отрисованный текст
  // "уезжает" вправо от расчётной позиции. Ошибка накапливается от
  // сегмента к сегменту, поэтому итоговый сдвиг у минут больше, чем у
  // часа, и визуально двоеточие оказывается внутри часовых цифр.
  int16_t hourX1, colonX1, minX1, by;
  uint16_t hourW, hourH, colonW, colonH, minW, minH;
  display->getTextBounds(hourBuf, 0, 0, &hourX1, &by, &hourW, &hourH);
  display->getTextBounds(":", 0, 0, &colonX1, &by, &colonW, &colonH);
  display->getTextBounds(minBuf, 0, 0, &minX1, &by, &minW, &minH);

  // GAP - зазор в пикселях слева и справа от двоеточия, чтобы оно не
  // соприкасалось с соседними цифрами (без зазора getTextBounds() даёт
  // геометрически точную, но визуально слипшуюся компоновку).
  const int GAP = 1; // <-- ПОДВИНУТЬ: расстояние между цифрами и двоеточием

  int totalWidth = hourW + GAP + colonW + GAP + minW;
  int startX = (PANEL_W - totalWidth) / 2;
  if (startX < 0) startX = 0; // целиком не влезает - прижимаем к левому краю, не обрезаем середину

  // Курсор для setCursor() - это НЕ то же самое, что желаемая позиция
  // левого края видимого текста: чтобы реальный (закрашенный) пиксель
  // начинался ровно в нужной точке, из неё нужно вычесть x1 этого
  // конкретного сегмента.
  int hourInkX = startX;
  int colonInkX = hourInkX + hourW + GAP;
  int minInkX = colonInkX + colonW + GAP;

  int hourX = hourInkX - hourX1;
  int colonX = colonInkX - colonX1;
  int minX = minInkX - minX1;

  display->setCursor(hourX + offsetX, 28 + offsetY);
  display->print(hourBuf);

  // Двоеточие мигает через общий colonState (тикает раз в секунду в
  // main.cpp) - надёжнее, чем в оригинале, где мигание делалось
  // подменой символа ":" на ";" (нестандартный трюк под конкретный шрифт).
  // Позиция colonX посчитана одинаково независимо от того, видно
  // двоеточие сейчас или нет - остальные цифры не "прыгают" при миганьи.
  if (colonState) {
    display->setCursor(colonX + offsetX, 28 + offsetY);
    display->print(":");
  }

  display->setCursor(minX + offsetX, 28 + offsetY);
  display->print(minBuf);

  display->setFont();
}
