#pragma once
#include <Arduino.h>

// ---- Расчёт направления ветра (16 точек) из градусов Open-Meteo ----
// Возвращает те же русские названия, что были в старом проекте,
// но теперь с полными 16 направлениями (а не 8, как сейчас в weather.cpp).
String windDirectionFromDegrees(int deg);

// ---- Иконки ветра: 8 базовых направлений есть, 8 промежуточных - миганием ----
enum class WindIcon : uint8_t {
  N, NE, E, SE, S, SW, W, NW,
  NONE // штиль / не распознано
};

struct WindIconPlan {
  WindIcon primary;
  WindIcon secondary;   // == primary, если мигание не нужно
  bool blink;
};

// dirRus - строка вида "Северный", "Западный-Юго-Западный" и т.п.
// (то, что возвращает windDirectionFromDegrees)
WindIconPlan getWindIconPlan(const String &dirRus);

// Рисует стрелку компаса в (cx,cy). Если направление промежуточное -
// САМА функция мигает между двумя соседними направлениями раз в 500мс,
// используя millis() (НЕ delay() - не блокирует loop/OTA/watchdog).
// Вызывать каждый кадр, как остальные drawXxx() в screen.cpp/forecast.cpp.
void drawWindCompassIcon(int cx, int cy, const String &dirRus);
