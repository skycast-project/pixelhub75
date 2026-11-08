#include "screen.h"
#include "globals.h"
#include "fonts.h"
#include "utils.h"
#include "wind.h"
#include "sensors.h"
#include "integrations.h"
#include "event_calendar.h"
#include <math.h>

// Кроме SCROLL_LINE_COUNT обычных строк есть ещё 3 служебные: "приём" (MQTT и Народмон)
// и календарь событий. Текст/цвет/скорость/вкл-выкл приёма хранит модуль integrations
// (страница /settings/receive), календаря - модуль event_calendar (страница /settings/calendar),
// поэтому globals.h/storage.cpp/scroll.html для них менять не нужно.
#define EXTRA_LINES 3
#define TOTAL_LINES (SCROLL_LINE_COUNT + EXTRA_LINES)
#define CAL_LINE (SCROLL_LINE_COUNT + 2)
static String extraLineText[2];   // MQTT и Народмон; календарь отдаёт текст сам

// Порядок показа: дата -> календарь -> остальные строки -> MQTT -> Народмон.
// currentScrollLine - это ПОЗИЦИЯ в этом порядке, lineIdx() переводит её в номер строки.
static int lineIdx(int pos) {
  if (pos == 0) return 0;
  if (pos == 1) return CAL_LINE;
  return pos - 1;
}

static const String &lineText(int pos) {
  int i = lineIdx(pos);
  if (i == CAL_LINE) return calendarLineText();
  return (i < SCROLL_LINE_COUNT) ? scrollLineText[i] : extraLineText[i - SCROLL_LINE_COUNT];
}
static uint16_t lineColor(int pos) {
  int i = lineIdx(pos);
  if (i < SCROLL_LINE_COUNT) return scrollLineColor[i];
  if (i == CAL_LINE) return 0xFFFF;   // календарь красит себя сам
  uint32_t c = integrationsRxLineColor(i - SCROLL_LINE_COUNT);
  return display->color565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}
static int lineSpeed(int pos) {
  int i = lineIdx(pos);
  if (i < SCROLL_LINE_COUNT) return scrollLineSpeed[i];
  if (i == CAL_LINE) return calendarLineSpeed();
  return integrationsRxLineSpeed(i - SCROLL_LINE_COUNT);
}

// Пересобирает отдельные строки (дата / погода сегодня / погода завтра /
// датчики "дома" / "Сейчас на улице") из текущих данных. Индивидуальные цвет/скорость каждой
// строки НЕ трогает - это отдельные настройки (scrollLineColor[]/
// scrollLineSpeed[]), задаются через веб-интерфейс и хранятся в config.json.
void updateFullScrollText() {
  scrollLineText[0] = "Сегодня " + String(weekdayNamesFull[wdayNum]) + " " + String(dayNum) + " " +
                       String(monthNames[monthNum]) + " " + String(year) + " года.";

  if (weatherEnabled) {
    scrollLineText[1] = weatherString;
    scrollLineText[2] = weatherStringZ;
  } else {
    scrollLineText[1] = "Погода отключена";
    scrollLineText[2] = "";
  }

  // Строка датчиков "дома" - показываем, только если датчик реально
  // найден (иначе строка пустая, drawScrollText() её сам пропустит).
  // BMP280 не умеет мерить влажность - подставляем "-" вместо числа.
  {
    // Собираем только те показатели, которые реально есть: у Wi-Fi датчика
    // может не быть давления, у BMP280 - влажности.
    String s3;
    if ((detectedSensor != SensorType::NONE || homeSourceActive) && !isnan(homeTemp)) {
      char tb[12];
      snprintf(tb, sizeof(tb), "%+.1f", homeTemp);
      s3 = "В помещении: " + String(tb) + "C";
    }
    if (!isnan(homeHumidity) && homeHumidity > 0) {
      if (s3.length()) s3 += ", ";
      s3 += "влажность " + String(homeHumidity, 0) + "%";
    }
    if (!isnan(homePressure) && homePressure > 100.0f) {
      if (s3.length()) s3 += ", ";
      s3 += "давление " + String(homePressure * 0.750062f, 0) + " мм рт.ст.";
    }
    if (s3.length()) s3 += "                ";   // отступ между повторами строки
    scrollLineText[3] = s3;
  }

  // Строка "Сейчас на улице" - ТОЛЬКО с уличного датчика DS18B20. Пока датчик не
  // отвечает (или показания устарели) - строка пустая, drawScrollText() её
  // пропускает. Интернет-погоду сюда не подставляем.
  if (outdoorTempValid) {
    char tb[12];
    snprintf(tb, sizeof(tb), "%+d", (int)round(outdoorTemp));
    scrollLineText[4] = "Сейчас на улице " + String(tb) + "C   ";
  } else {
    scrollLineText[4] = "";
  }

  // Пользователь может скрыть любую строку целиком через веб-интерфейс
  // (scrollLineEnabled[]) - гасим текст ПОСЛЕ обычной сборки, чтобы не
  // трогать логику выше. drawScrollText() и так пропускает пустые строки.
  for (int i = 0; i < SCROLL_LINE_COUNT; i++) {
    if (!scrollLineEnabled[i]) scrollLineText[i] = "";
  }

  // Строки приёма (MQTT / Народмон): пустые, если выключены или данные устарели
  for (int k = 0; k < 2; k++) extraLineText[k] = integrationsRxLineText(k);

  Serial.println("📝 строки бегущей строки пересобраны (" + String(TOTAL_LINES) + " шт.)");
}

// Рисует ТЕКУЩУЮ строку (currentScrollLine) её собственным цветом/скоростью.
// Когда строка добегает до конца - переключается на следующую (offset
// сбрасывается). Когда добежала ПОСЛЕДНЯЯ строка и цикл пошёл на новый
// круг (currentScrollLine вернулся к 0) - выставляется scrollLapCompleted,
// как раньше (main.cpp ждёт именно полного прохода ВСЕХ строк, прежде
// чем переключиться на экран прогноза/погоды).
void drawScrollText() {
  // Пропускаем пустые строки (например, "погода завтра", если погода
  // отключена) - сразу переходим к следующей, без показа пустого места
  int guard = 0;
  while (lineText(currentScrollLine).length() == 0 && guard < TOTAL_LINES) {
    currentScrollLine = (currentScrollLine + 1) % TOTAL_LINES;
    if (currentScrollLine == 0) scrollLapCompleted = true;
    guard++;
  }
  if (lineText(currentScrollLine).length() == 0) return; // все строки пустые

  display->setFont(&Heebo7pt8b);
  display->setTextSize(1);
  display->setTextColor(lineColor(currentScrollLine));

  bool isCalendar = (lineIdx(currentScrollLine) == CAL_LINE);
  String utf8 = isCalendar ? String("") : utf8rus(lineText(currentScrollLine));
  // Надёжный расчёт ширины: количество символов * 6 (подходит для Heebo)
  int textWidth = (isCalendar ? calendarLineChars() : (int)utf8.length()) * 6;

  static unsigned long lastMoveTime = 0;
  unsigned long now = millis();
  // Ползунок 1..10. Диапазон: speed=1 -> 52 мс/пиксель (это бывшая 3-я скорость прежней шкалы
  // 69..20 мс; две самые медленные ступени убраны), speed=10 -> 20 мс/пиксель (прежний максимум). Между ними шаги геометрические
  // (каждая следующая скорость быстрее предыдущей в одно и то же число раз),
  // поэтому на глаз регулировка получается плавной и ровной.
  // Хочется другие края - менять только эти две константы:
  const float SCROLL_SLOW_MS = 52.0f;   // скорость 1  <-- ПОДВИНУТЬ
  const float SCROLL_FAST_MS = 20.0f;   // скорость 10 <-- ПОДВИНУТЬ
  int sp = lineSpeed(currentScrollLine);
  if (sp < 1) sp = 1;
  if (sp > 10) sp = 10;
  unsigned long interval = (unsigned long)(SCROLL_SLOW_MS * powf(SCROLL_FAST_MS / SCROLL_SLOW_MS, (sp - 1) / 9.0f) + 0.5f);

  if (now - lastMoveTime >= interval) {
    lastMoveTime = now;
    scrollOffset -= 1;

    if (scrollOffset < -textWidth) {
      scrollOffset = startOffset;
      currentScrollLine = (currentScrollLine + 1) % TOTAL_LINES;
      if (currentScrollLine == 0) {
        scrollLapCompleted = true; // все 3 строки прошли полный круг
      }
    }
  }

  // Бегущую строку держим на 1 пиксель ниже базового scrollY
  // (было +2, поднято на 1 px вверх). Правка локальная, чтобы не сдвигать
  // другие экраны, которые могут использовать scrollY.
  const int SCROLL_Y_OFFSET = 1;  // <-- ПОДВИНУТЬ: на сколько пикселей опустить строку

  if (isCalendar) {
    calendarDrawLine(scrollOffset, scrollY + SCROLL_Y_OFFSET);   // радуга по буквам
  } else {
    display->setCursor(scrollOffset, scrollY + SCROLL_Y_OFFSET);
    display->print(utf8);
  }
}

// Полоска дней недели убрана с этого экрана - как в оригинале, она
// показывается только в ночном режиме (см. drawWeekdayDots() в
// night_screen.cpp, уже реализовано и вызывается там).

void drawScreen() {
  // Часы, минуты, секунды и день недели опускаем на 1 px вниз.
  // Дата и месяц (ниже) рисуются без этой поправки - остаются на месте.
  const int CLOCK_Y_OFFSET = 1;  // <-- ПОДВИНУТЬ: на сколько пикселей опустить часы/минуты/секунды/день недели

  char buf[3];
  display->setFont(clockFontVariants[clockFontIndex]);
  display->setTextSize(1);
  display->setTextColor(clockColor);
  sprintf(buf, "%02d", hours);
  display->setCursor(1, 15 + SHIFT_Y + CLOCK_Y_OFFSET);
  display->print(buf);

  if (colonState) {
    display->setCursor(22, 12 + SHIFT_Y + CLOCK_Y_OFFSET);
    display->print(":");
  }

  display->setTextColor(clockColor);
  sprintf(buf, "%02d", minutes);
  display->setCursor(27, 15 + SHIFT_Y + CLOCK_Y_OFFSET);
  display->print(buf);

  if (showSeconds) {
    display->setFont(secFontVariants[secFontIndex]);
    display->setTextColor(secondsColor);
    display->setCursor(49, 15 + SHIFT_Y + CLOCK_Y_OFFSET);
    sprintf(buf, "%02d", seconds);
    display->print(buf);
  }

  // Выходные (0 = воскресенье, 6 = суббота): другой цвет, если включено
  bool weekendNow = weekendColorEnabled && (wdayNum == 0 || wdayNum == 6);

  display->setFont(secFontVariants[secFontIndex]);
  display->setTextColor(weekendNow ? weekendDayColor : dayColor);
  display->setCursor(49, 8 + SHIFT_Y + CLOCK_Y_OFFSET);
  display->print(dayNames[wdayNum]);

  display->setFont(secFontVariants[secFontIndex]);
  display->setTextColor(weekendNow ? weekendDateColor : dateColor);
  int numX = dateNumX[monthNum][dayNum < 10 ? 0 : 1];
  // Полоску дней недели убрали с этого экрана (перенесена в night_screen.cpp,
  // вниз ночного режима - как было задумано в оригинале). Конфликта по
  // строке 0 больше нет, можно вернуть y=5. <-- ПОДВИНУТЬ, если хочется
  // число/месяц повыше/пониже - высота матрицы 32, бегущий текст с y=29.
  display->setCursor(numX, 5);
  display->print(dayNum);

  display->setFont(&Heebo7pt8b);
  display->setTextColor(monthColor);
  display->print(utf8rus(String(monthNames[monthNum])));

  drawScrollText();
  display->setFont();
}
