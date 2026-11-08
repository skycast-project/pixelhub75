#pragma once
#include <Arduino.h>

// Преобразование HEX-цвета ("#RRGGBB") в 16-битный цвет матрицы (RGB444)
uint16_t hexToColor(const String &hex);

// Перекодировка UTF-8 (кириллица) под однобайтовую кодировку шрифтов
String utf8rus(const String &source);
