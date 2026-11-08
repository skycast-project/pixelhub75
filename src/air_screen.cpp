#include "air_screen.h"
#include "globals.h"
#include "fonts.h"
#include "pins.h"
#include "utils.h"
#include <math.h>

static uint16_t levelColor(float co2) {
  if (co2 < 600)  return display->color565(0, 200, 0);
  if (co2 < 800)  return display->color565(220, 200, 0);
  if (co2 < 1200) return display->color565(255, 120, 0);
  return display->color565(255, 40, 40);
}

void drawAirQualityScreen() {
  display->setFont(&Heebo7pt8b);
  display->setTextColor(airTitleColor);
  display->setCursor(10, 6);
  display->print(utf8rus("воздух"));

  // Подписи строго латиницей, стандартным шрифтом 5x7 (он точно есть в прошивке)
  display->setFont();
  display->setTextSize(1);
  display->setTextColor(airCo2Color);
  display->setCursor(1, 15);
  display->print("eCO2");
  display->setTextColor(display->color565(255, 255, 255));
  display->setCursor(30, 15);
  display->print(String((int)airCo2).c_str());

  display->setTextColor(airTvocColor);
  display->setCursor(1, 24);
  display->print("TVOC");
  display->setTextColor(display->color565(255, 255, 255));
  display->setCursor(30, 24);
  display->print(String((int)airTvoc).c_str());

  // полосы справа: высота и цвет зависят от eCO2/TVOC
  int h1 = constrain((int)(airCo2 / 1500.0f * 30.0f), 3, 31);
  display->fillRect(53, 32 - h1, 4, h1, levelColor(airCo2));
  int h2 = constrain((int)(airTvoc / 1000.0f * 30.0f), 3, 31);
  display->fillRect(59, 32 - h2, 4, h2, airBarColor);
  display->setFont();
}
