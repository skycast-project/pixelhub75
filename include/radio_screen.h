#pragma once
#include <Arduino.h>

// Экран матрицы, показываемый пока играет радио (radioState.active==true).
// Полностью перехватывает экран - как большие часы/ночной режим.
void drawRadioScreen();
