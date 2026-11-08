#pragma once
#include <Arduino.h>

// Будильники на DFPlayer (папка /02, файлы 001.mp3..009.mp3)
void alarmDfpInit();
void alarmDfpTick();          // вызывать раз в секунду
void alarmDfpStop();
bool alarmDfpActive();
