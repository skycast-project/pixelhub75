#include "rtc_clock.h"
#include "globals.h"
#include <Wire.h>
#include <RTClib.h>
#include <time.h>
#include <sys/time.h>

static RTC_DS3231 rtc;
static bool s_ok = false;
static uint32_t s_last = 0;

void rtcInit() {
  s_ok = rtc.begin(&Wire);
  if (!s_ok) { Serial.println("ℹ️ DS3231 не найден"); return; }
  DateTime now = rtc.now();
  time_t sys = time(nullptr);
  bool sysValid = (sys > 1700000000);          // после 2023 года
  if (!sysValid && now.year() >= 2024) {
    struct timeval tv = { (time_t)now.unixtime(), 0 };
    settimeofday(&tv, nullptr);
    Serial.printf("✅ DS3231: время восстановлено %02d.%02d.%04d %02d:%02d\n",
                  now.day(), now.month(), now.year(), now.hour(), now.minute());
  } else {
    Serial.println("✅ DS3231 найден (время берём из сети)");
  }
}

void rtcTick() {
  if (!s_ok) return;
  uint32_t ms = millis();
  if (s_last && ms - s_last < 3600UL * 1000UL) return;   // раз в час
  s_last = ms;
  time_t sys = time(nullptr);
  if (sys < 1700000000) return;
  rtc.adjust(DateTime((uint32_t)sys));
}

bool rtcPresent() { return s_ok; }
