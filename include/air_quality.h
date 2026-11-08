#pragma once
#include <Arduino.h>

// SGP30: качество воздуха (eCO2 ppm, TVOC ppb)
void airQualityInit();
void airQualityTick();          // вызывать из loop(), внутри свой интервал
bool airQualityReady();         // экран «Качество воздуха» показывать
