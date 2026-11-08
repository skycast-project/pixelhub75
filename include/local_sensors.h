#pragma once
#include <Arduino.h>

class AsyncWebServer;

// ============================================================
// ЛОКАЛЬНЫЕ ДАТЧИКИ ПО WI-FI
//  - датчики (ESP8266/ESP32 в той же сети) сами присылают значения информеру
//    обычным запросом:  http://IP-информера/api/sensor?id=street&t=-5.2&h=80&b=3.9
//      id - имя датчика латиницей (как вписано в слот на странице «Датчики»)
//      t  - температура, °C         h - влажность, %
//      p  - давление, гПа           b - напряжение батарейки, В
//      k  - пароль (если задан на странице)
//    Все поля, кроме id, необязательны: датчик шлёт то, что умеет.
//  - до LS_SLOTS датчиков, настройки хранятся в NVS (заливка файловой системы их не стирает);
//  - значения живут только в памяти, устаревшие (давно нет данных) показываются прочерком;
//  - датчик можно назначить «уличным»: тогда его температура идёт на ночной экран,
//    в строку «Сейчас на улице» и в MQTT/Народмон вместо DS18B20 (пока данные свежие).
// Страница настройки: /settings/sensors.  Экран «Комнаты»: rooms_screen.cpp.
// ============================================================

#define LS_SLOTS 4

void localSensorsInit();               // загрузить настройки (вызвать в setup)
void localSensorsRegisterWeb(AsyncWebServer &srv);
void localSensorsTick();               // раз в секунду: «уличный» датчик -> outdoorTemp

// ---- Для экрана «Комнаты» ----
struct LsView {
  String name;          // название на экране (UTF-8)
  bool fresh = false;   // данные свежие
  bool hasT = false, hasH = false, hasP = false;
  float t = 0, h = 0, pMm = 0;   // температура, влажность, давление в мм рт. ст. (с поправками)
  bool lowBatt = false; // батарейка ниже заданного порога
  uint8_t second = 0;   // что показывать во второй строке: 0 авто, 1 влажность, 2 давление
};
// Показания для экрана «Дома» из выбранного Wi-Fi датчика (если настроен).
// false - использовать встроенный I2C датчик.
bool localSensorsHome(float &t, float &h, float &p);
bool localSensorsHomeConfigured();
bool localSensorsRoomsEnabled();       // экран «Комнаты» включён
int localSensorsShownCount();          // сколько датчиков выводится на экран
bool localSensorsGetShown(int n, LsView &v);   // n-й из выводимых
// То же, но без датчика, выбранного источником для экрана «Дома».
int localSensorsRoomsCount();
bool localSensorsRoomsGet(int n, LsView &v);

// ---- Внутреннее, открыто для тестов на компьютере ----
String localSensorsCleanId(const String &in);

// Забрать показания Wi-Fi датчиков с домашней станции (раз в минуту).
// Дома — по HTTP, вне дома — через tuna.
void localSensorsRemoteTick();
