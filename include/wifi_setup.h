#pragma once
#include <Arduino.h>

// Пытается подключиться к сохранённой сети (~10 сек). Если не получилось -
// поднимает точку доступа "PixelHub75-Setup" с captive-порталом для ввода
// новой сети. Возвращает true при удачном подключении к обычной сети,
// false - если ушли в режим точки доступа (тогда setup() должен
// остановиться, а loop() - крутить только handleWifiSetupLoop()).
bool connectWiFiOrStartAP();

// Вызывать в loop(), пока активна точка доступа - обслуживает DNS-редирект
// captive portal. Ничего не делает, если AP не поднят.
void handleWifiSetupLoop();

bool isWifiSetupApActive();
