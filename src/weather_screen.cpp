#include "weather_screen.h"
#include <math.h>
#include <stdio.h>
#include "globals.h"
#include "fonts.h"
#include "utils.h"
#include "forecast.h"
#include "wind.h"

// ============================================================
// ЭКРАН "ПОГОДА СЕЙЧАС" - реконструкция оригинального getNow()/getImeg()
// (Pogoda_ekr.ino) под новую архитектуру проекта.
//
// ПОДСКАЗКА ДЛЯ РУЧНОЙ ПОДГОНКИ КООРДИНАТ:
// setCursor(x, y) - x это столбец слева направо (0..63), y это строка
// сверху вниз (0..31). Чтобы подвинуть элемент ВНИЗ - УВЕЛИЧИВАЙТЕ y.
// Чтобы подвинуть ВПРАВО - увеличивайте x. У каждого setCursor() ниже
// есть комментарий, что именно он двигает.
// ============================================================

// Завтрашнее число месяца - та же формула переноса на конец месяца,
// что была в оригинальном getNow() (без учёта високосного февраля,
// как и в оригинале)
static int tomorrowDayNum() {
  bool isLastDay =
    ((monthNum == 3 || monthNum == 5 || monthNum == 8 || monthNum == 10) && dayNum == 30) ||
    ((monthNum == 0 || monthNum == 2 || monthNum == 4 || monthNum == 6 ||
      monthNum == 7 || monthNum == 9 || monthNum == 11) && dayNum == 31) ||
    (monthNum == 1 && dayNum == 28);
  return isLastDay ? 1 : dayNum + 1;
}

// Знак температуры всегда своим цветом, независимо от цвета цифр: плюс красный, минус синий.
// Цвета подбираются здесь: <-- ПОДВИНУТЬ
static const bool SIGN_COLORS_ON = true;     // false - знак того же цвета, что и цифры
static inline uint16_t signPlusColor()  { return display->color565(255, 0, 0); }
static inline uint16_t signMinusColor() { return display->color565(0, 120, 255); }

// Печатает строку с температурой от текущей позиции курсора: знак (+ или -) своим цветом, остальное цветом цифр.
static void printSignedTemp(const char *buf, uint16_t numColor) {
  char first = buf[0];
  if (SIGN_COLORS_ON && (first == '+' || first == '-')) {
    display->setTextColor(first == '+' ? signPlusColor() : signMinusColor());
    display->print(first);
    buf++;
  }
  display->setTextColor(numColor);
  display->print(buf);
}

void drawWeatherNowScreen() {
  // --- Заголовок "погода" --- (x=14, y=5)  [было y=7, поднято на 2 px вверх]
  display->setFont(&Heebo7pt8b);
  display->setTextColor(weatherTitleColor);
  display->setCursor(14, 5);   // <-- ПОДВИНУТЬ ЗАГОЛОВОК "погода"
  display->print(utf8rus("погода"));
  display->setFont();

  display->setFont(&TomThumb);

  // --- Сегодняшнее число (слева) --- (x=2 или 7 в завис. от 1/2 цифр, y=7)  [было y=6, опущено на 1 px]
  display->setTextColor(weatherDateColor);
  display->setCursor(dayNum >= 10 ? 2 : 7, 7);   // <-- ПОДВИНУТЬ ЧИСЛО "СЕГОДНЯ"
  display->print(dayNum);

  // --- Завтрашнее число (справа) --- (x=53 или 54, y=7)  [было y=6, опущено на 1 px]
  int tomorrow = tomorrowDayNum();
  display->setTextColor(weatherDateColor);
  display->setCursor(tomorrow >= 10 ? 53 : 54, 7);   // <-- ПОДВИНУТЬ ЧИСЛО "ЗАВТРА"
  display->print(tomorrow);

  // --- Время h:mm (по центру) --- (x=22 или 26 в завис. от 1/2 цифр часа, y=14)  [было y=16, поднято на 2 px вверх]
  char timeBuf[8];
  snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", hours, minutes);
  display->setTextColor(weatherTimeColor);
  display->setCursor(hours < 10 ? 26 : 22, 14);   // <-- ПОДВИНУТЬ ВРЕМЯ
  display->print(timeBuf);

  // --- Текущая температура (низ-слева) --- x зависит от знака (та же
  // логика, что в оригинале), y=29 для всех веток
  int tempNow = (int)round(temperaturedarc);
  char buf[12];
  display->setTextColor(weatherTempNowColor);
  if (tempNow > 0) {
    display->setCursor(1, 29);   // <-- ПОДВИНУТЬ ТЕМПЕРАТУРУ "СЕЙЧАС" (положительная)
    snprintf(buf, sizeof(buf), "+ %dc", tempNow);
  } else if (tempNow == 0) {
    display->setCursor(3, 29);   // <-- ПОДВИНУТЬ ТЕМПЕРАТУРУ "СЕЙЧАС" (ровно 0)
    snprintf(buf, sizeof(buf), "%dc", tempNow);
  } else {
    display->setCursor(1, 29);   // <-- ПОДВИНУТЬ ТЕМПЕРАТУРУ "СЕЙЧАС" (отрицательная)
    snprintf(buf, sizeof(buf), "%dc", tempNow);
  }
  printSignedTemp(buf, weatherTempNowColor);

  // --- Максимальная температура на завтра (низ-справа) --- 5 веток
  // позиции по величине/знаку (как в оригинале), y=29 для всех
  int tempMaxTomorrow = (int)round(temperatureHighdarc);
  char buf2[12];
  display->setTextColor(weatherTempTomorrowColor);
  if (tempMaxTomorrow >= 10) {
    display->setCursor(39, 29);   // <-- ПОДВИНУТЬ ТЕМПЕРАТУРУ "ЗАВТРА" (>=10)
    snprintf(buf2, sizeof(buf2), "+ %dc", tempMaxTomorrow);
  } else if (tempMaxTomorrow < -10) {
    display->setCursor(40, 29);   // <-- ПОДВИНУТЬ ТЕМПЕРАТУРУ "ЗАВТРА" (<-10)
    snprintf(buf2, sizeof(buf2), "%dc", tempMaxTomorrow);
  } else if (tempMaxTomorrow > 0) {
    display->setCursor(44, 29);   // <-- ПОДВИНУТЬ ТЕМПЕРАТУРУ "ЗАВТРА" (0..10)
    snprintf(buf2, sizeof(buf2), "+ %dc", tempMaxTomorrow);
  } else if (tempMaxTomorrow < 0) {
    display->setCursor(44, 29);   // <-- ПОДВИНУТЬ ТЕМПЕРАТУРУ "ЗАВТРА" (-10..0)
    snprintf(buf2, sizeof(buf2), "%dc", tempMaxTomorrow);
  } else {
    display->setCursor(48, 29);   // <-- ПОДВИНУТЬ ТЕМПЕРАТУРУ "ЗАВТРА" (ровно 0)
    snprintf(buf2, sizeof(buf2), "%dc", tempMaxTomorrow);
  }
  printSignedTemp(buf2, weatherTempTomorrowColor);

  display->setFont();

  // --- Иконки погоды: сегодня слева, завтра справа ---
  // Координаты здесь - ЦЕНТР иконки 10x10 (не левый верхний угол!),
  // сдвиг картинки внутри delaWeatherIcon() всегда -5/-5 от переданных x,y
  drawWeatherIcon(forecastWeathercode[0], 12, 18);   // <-- ПОДВИНУТЬ ИКОНКУ "СЕГОДНЯ"
  drawWeatherIcon(forecastWeathercode[1], 52, 18);   // <-- ПОДВИНУТЬ ИКОНКУ "ЗАВТРА"

  // --- Компас ветра --- координаты тоже ЦЕНТР (9x9), сдвиг -4/-4 внутри функции
  drawWindCompassIcon(32, 23, degString);   // <-- ПОДВИНУТЬ КОМПАС ВЕТРА (опущен на 2 px, было y=21)
}
