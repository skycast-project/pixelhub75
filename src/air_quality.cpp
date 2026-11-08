#include "air_quality.h"
#include "globals.h"
#include <Wire.h>
#include <Adafruit_SGP30.h>

static Adafruit_SGP30 sgp;
static bool s_ok = false;
static uint32_t s_last = 0;

void airQualityInit() {
  s_ok = sgp.begin(&Wire);
  if (s_ok) Serial.println("✅ Найден SGP30 (качество воздуха: eCO2, TVOC)");
  else Serial.println("ℹ️ SGP30 не найден (экран качества воздуха отключён)");
}

void airQualityTick() {
  if (!s_ok) return;
  uint32_t now = millis();
  if (s_last && now - s_last < 1000) return;
  s_last = now;
  if (!sgp.IAQmeasure()) return;
  airCo2 = sgp.eCO2;
  airTvoc = sgp.TVOC;
  airValid = true;
}

bool airQualityReady() { return s_ok && airValid; }
