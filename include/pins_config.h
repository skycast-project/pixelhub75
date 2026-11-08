#pragma once

#include <Arduino.h>
#include "config_pins.h"

// ============================================================
//  ПИНЫ МАТРИЦЫ, ИЗМЕНЯЕМЫЕ ИЗ ВЕБ-ИНТЕРФЕЙСА
//
//  Значения по умолчанию берутся из include/config_pins.h.
//  Сохранённые в NVS пины имеют приоритет (применяются при загрузке).
//  Страница настройки: /settings/pins.
// ============================================================

struct MatrixPins {
  int8_t r1, g1, b1;
  int8_t r2, g2, b2;
  int8_t a, b, c, d, e;
  int8_t lat, oe, clk;
};

MatrixPins pinsDefaults();
MatrixPins pinsLoad();
bool pinsSave(const MatrixPins &p);
bool pinsValidate(const MatrixPins &p, String &error);
void pinsReset();
