#pragma once
#include <Arduino.h>

class AsyncWebServer;

// ============================================================
// Календарь событий для бегущей строки
//  - события хранятся во флеш-памяти (NVS) и дублируются на SD-карте (/calendar.json);
//    заливка файловой системы их не стирает; с SD-картой событий до 60;
//  - строка идёт сразу после даты: "Сегодня: День рождения мамы   Завтра: ..."
//  - напоминание за N дней, встроенные праздники России (по желанию);
//  - текст события может переливаться радугой по буквам, как заставка.
// Страница настройки: /settings/calendar
// ============================================================

void calendarInit();   // загрузить события из NVS (вызвать в setup)
void calendarTick();   // вызывать раз в секунду: пересобирает строку при смене даты/правках
void calendarRegisterWeb(AsyncWebServer &srv);

// Для бегущей строки (screen.cpp)
const String &calendarLineText();   // обычный UTF-8 текст строки ("" - показывать нечего)
int calendarLineSpeed();            // скорость 1..10
int calendarLineChars();            // длина строки в символах шрифта (ширина = chars * 6)
// Рисует строку с левым краем x и базовой линией y: радужные события по буквам,
// остальные своим цветом. Шрифт и размер выставляет вызывающий.
void calendarDrawLine(int x, int y);

// ---- Внутреннее, открыто для тестов на компьютере ----
struct CalEvent {
  uint8_t d = 1, m = 1;       // день, месяц (1..12)
  uint16_t y = 0;             // год; 0 - повторять каждый год
  uint16_t by = 0;            // год рождения/начала (для повторяющихся): в строке "(35 лет)"; 0 - не показывать
  bool rainbow = true;        // true - радуга по буквам, false - цвет color
  uint32_t color = 0xFFFFFF;  // 0xRRGGBB
  String text;
};

struct CalConfig {
  bool enabled = true;
  uint8_t remindDays = 2;     // за сколько дней начинать напоминать (0 - только в сам день)
  bool holidays = false;      // показывать государственные праздники
  bool remote = true;         // брать праздники и события с сервера (домашняя сборка)
  uint8_t speed = 5;          // 1..10
};

struct CalSeg {
  String raw;        // UTF-8
  String conv;       // после utf8rus (по одному байту на символ)
  bool rainbow;
  uint32_t color;
};

// Собирает сегменты строки на указанную дату (месяц 1..12)
void calendarBuildSegments(const CalConfig &cfg, const std::vector<CalEvent> &events,
                           int y, int m, int d, std::vector<CalSeg> &out);
// Через сколько дней событие (0 - сегодня), -1 - уже прошло (для разового)
int calendarDaysUntil(const CalEvent &e, int y, int m, int d);
// Склонение: 1 день, 2 дня, 5 дней
String calendarDayWord(int n);
// Сериализация (компактный JSON) и разбор
String calendarToJson(const CalConfig &cfg, const std::vector<CalEvent> &events);
bool calendarFromJson(const String &json, CalConfig &cfg, std::vector<CalEvent> &events);
// Склонение: 1 год, 2 года, 5 лет
String calendarYearWord(int n);
// Возраст на ближайшее наступление повторяющегося события (0 - не показывать)
int calendarAgeOn(const CalEvent &e, int y, int m, int d);
// true, если SD-карта найдена и копия событий ведётся
bool calendarSdActive();
