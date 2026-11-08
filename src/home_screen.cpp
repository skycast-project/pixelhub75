#include "home_screen.h"
#include <math.h>
#include <stdio.h>
#include "globals.h"
#include "fonts.h"
#include "utils.h"
#include "icons.h"
#include "icon_boost.h"
#include "sensors.h"

// ============================================================
// ЭКРАН "ДОМА:" - реконструкция getNow1() из Dom_ekr.ino
//
// ПОДСКАЗКА ДЛЯ РУЧНОЙ ПОДГОНКИ КООРДИНАТ - см. weather_screen.cpp,
// тот же принцип: y больше = ниже, x больше = правее.
//
// Раскладка (64x32) - координаты как в оригинале:
//   "дома:" (заголовок)           - верх-слева
//   время h:mm                    - верх-справа
//   барометр (иконка 12x12)       - (0,7)
//   давление (текст)              - под барометром, (13,16)
//   термометр (иконка 12x12)      - (28,19)
//   температура (текст)           - справа от термометра, (40,28)
//   капля влажности (иконка 10x12)- (1,20)
//   влажность (текст)             - справа от капли, (13,28)
//   "ДОМ" с окном (иконка 12x12)  - (51,8)
// ============================================================

void drawHomeScreen() {
  // Что реально есть: если показателя нет, не рисуем ни значение, ни значок.
  const bool anyHome = detectedSensor != SensorType::NONE || homeSourceActive;
  const bool hasT = anyHome && !isnan(homeTemp);
  const bool hasP = anyHome && !isnan(homePressure) && homePressure > 100.0f;
  const bool hasH = anyHome && !isnan(homeHumidity) && homeHumidity > 0.0f;
  display->setFont(&Heebo7pt8b);
  display->setTextColor(homeTitleColor);
  display->setCursor(1, 5);   // заголовок «дома:» опущен на 5 px
  display->print(utf8rus("дома:"));
  display->setFont();

  // Время (верх-справа), TomThumb - как на экране погоды
  display->setFont(&TomThumb);
  display->setTextColor(homeTimeColor);
  char timeBuf[8];
  snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", hours, minutes);
  display->setCursor(hours < 10 ? 45 : 41, 5);   // время опущено на 5 px
  display->print(timeBuf);

  // --- Иконка "ДОМ" с мигающим окном --- (51,8), 12x12, мигает раз в 500мс
  static unsigned long lastHouseToggle = 0;
  static bool houseFrame2 = false;
  unsigned long now = millis();
  if (now - lastHouseToggle >= 500) {
    lastHouseToggle = now;
    houseFrame2 = !houseFrame2;
  }
  drawRGBBitmapBoost(51, 8, houseFrame2 ? image_data_Image29 : image_data_Image28, 12, 12);   // <-- ПОДВИНУТЬ ИКОНКУ "ДОМ"

  // --- Барометр --- (0,7), 12x12, мигает раз в 300мс (как в оригинале)
  static unsigned long lastBaroToggle = 0;
  static bool baroFrame2 = false;
  if (now - lastBaroToggle >= 300) {
    lastBaroToggle = now;
    baroFrame2 = !baroFrame2;
  }
  if (hasP) drawRGBBitmapBoost(0, 7, baroFrame2 ? image_data_Image25 : image_data_Image24, 12, 12);   // барометр только при наличии давления

  // --- Термометр --- (28,19), 12x12, мигает раз в 300мс
  static unsigned long lastThermToggle = 0;
  static bool thermFrame2 = false;
  if (now - lastThermToggle >= 300) {
    lastThermToggle = now;
    thermFrame2 = !thermFrame2;
  }
  if (hasT) drawRGBBitmapBoost(28, 19, thermFrame2 ? image_data_Image23 : image_data_Image22, 12, 12);   // термометр только при наличии температуры

  // --- Капля влажности --- (1,20), 10x12, мигает раз в 300мс
  static unsigned long lastDropToggle = 0;
  static bool dropFrame2 = false;
  if (now - lastDropToggle >= 300) {
    lastDropToggle = now;
    dropFrame2 = !dropFrame2;
  }
  if (hasH) drawRGBBitmapBoost(1, 20, dropFrame2 ? image_data_Image27 : image_data_Image26, 10, 12);   // капля только при наличии влажности

  // --- Текстовые значения ---
  display->setFont(&TomThumb);
  char buf[12];

  // Давление - под барометром. BMP280 умеет давление, показываем всегда,
  // если хоть какой-то датчик найден
  if (hasP) {
    display->setTextColor(homePressureColor);
    snprintf(buf, sizeof(buf), "%d", (int)round(homePressure * 0.750062f));   // гПа -> мм рт. ст.
    display->setCursor(13, 16);   // <-- ПОДВИНУТЬ ДАВЛЕНИЕ
    display->print(buf);
  }

  // Температура - справа от термометра
  if (hasT) {
    display->setTextColor(homeTempColor);
    int t = (int)round(homeTemp);
    snprintf(buf, sizeof(buf), "%+dc", t);
    display->setCursor(40, 28);   // <-- ПОДВИНУТЬ ТЕМПЕРАТУРУ
    display->print(buf);
  }

  // Влажность - справа от капли. У BMP280 влажности нет - показываем "-"
  if (hasH) {
    display->setTextColor(homeHumidityColor);
    display->setCursor(13, 28);   // влажность - только если датчик её умеет
    snprintf(buf, sizeof(buf), "%d%%", (int)round(homeHumidity));
    display->print(buf);
  }

  display->setFont();
}
