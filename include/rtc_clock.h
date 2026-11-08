#pragma once
// DS3231: хранит время между выключениями, синхронизируется с системой
void rtcInit();
void rtcTick();
bool rtcPresent();
