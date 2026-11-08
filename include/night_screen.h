#pragma once
#include <cstdint>
// Проверяет расписание ночного режима (nightOnHour/Minute..nightOffHour/
// Minute, с оборачиванием через полночь, как в оригинале getNreg()/
// getYtro() - hon/hof). Вызывать РАЗ В СЕКУНДУ.
void checkNightSchedule();

// true, пока активен ночной ИЛИ утренний переход - main.cpp подменяет
// этим обычную ротацию экранов (как с bigClockActive).
bool isNightModeActive();

// Рисует текущую фазу (засыпание/сон/пробуждение). Вызывать КАЖДЫЙ
// кадр, пока isNightModeActive() == true. Полностью неблокирующая -
// вся анимация идёт через millis(), ни одного delay() внутри (в
// отличие от оригинала, где use блокирующие delay(300) и т.п.).
void drawNightScreen();

// Яркость ночного экрана (часы, градусник, температура, подпись), 10..100 %.
// 100 = цвета ровно такие, как выбраны в настройках; меньше - приглушает.
// Хранится в config.json, меняется на странице "Ночь/утро".
extern uint8_t nightScreenBrightnessPercent;
