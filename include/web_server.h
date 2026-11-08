#pragma once
#include <ESPAsyncWebServer.h>

// ===== ШАБЛОНЫ =====
String loadTemplate(const String &path);
void processTemplate(String &html);

// ===== ОБРАБОТЧИКИ HTTP =====
void handleRoot(AsyncWebServerRequest *request);

void handleClockSettings(AsyncWebServerRequest *request);
void handleClockSettingsPost(AsyncWebServerRequest *request);

void handleScrollSettings(AsyncWebServerRequest *request);
void handleScrollSettingsPost(AsyncWebServerRequest *request);

void handleWifiSettings(AsyncWebServerRequest *request);
void handleWifiSettingsPost(AsyncWebServerRequest *request);

void handleBrightnessSettings(AsyncWebServerRequest *request);
void handleBrightnessSettingsPost(AsyncWebServerRequest *request);

void handleWeatherSettings(AsyncWebServerRequest *request);
void handleWeatherSettingsPost(AsyncWebServerRequest *request);

void handleAlarmSettings(AsyncWebServerRequest *request);
void handleAlarmSettingsPost(AsyncWebServerRequest *request);

void handleBigClockSettings(AsyncWebServerRequest *request);
void handleBigClockSettingsPost(AsyncWebServerRequest *request);

void handleNightSettings(AsyncWebServerRequest *request);
void handleNightSettingsPost(AsyncWebServerRequest *request);

// Регистрирует все маршруты на глобальном server и вызывает server.begin()
void setupWebServer();
