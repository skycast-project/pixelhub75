// ============================================================
// PixelHub 75 – Версия 4.9.2 (надёжный расчёт ширины + отладка)
// Разложено на модули под PlatformIO из одного .ino-файла
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>

#include "globals.h"
#include "display_manager.h"
#include "weather.h"
#include "storage.h"
#include "screen.h"
#include "web_server.h"
#include "forecast.h"
#include "watchdog.h"
#include "weather_screen.h"
#include "sensors.h"
#include "air_quality.h"
#include "air_screen.h"
#include "dfplayer.h"
#include "alarm_df.h"
#include "fonts.h"
#include "web_send.h"
#include "rtc_clock.h"
#include "outdoor_sensor.h"
#include "home_screen.h"
#include "big_clock.h"
#include "night_screen.h"
#include "wifi_setup.h"
#include "integrations.h"
#include "ota_update.h"
#include "event_calendar.h"
#include "local_sensors.h"
#include "rooms_screen.h"
#include "screen_fx.h"


// Сколько показывается экран прогноза после того, как бегущая строка
// на основном экране полностью прошла один круг, мс
static const unsigned long FORECAST_SCREEN_DURATION = 6000;

// Сколько показывается экран "погода сейчас" (копия оригинального getNow())
// сразу после прогноза, мс
static const unsigned long WEATHER_NOW_SCREEN_DURATION = 6000;

// Сколько показывается домашний экран "дома:" (копия getNow1()) - идёт
// последним в цепочке, после погоды сейчас, мс
static const unsigned long HOME_SCREEN_DURATION = 6000;

// Экран "Комнаты" (локальные датчики по Wi-Fi): столько показывается каждая страница (по 2 датчика), мс
static const unsigned long ROOMS_SCREEN_DURATION = 8000;  // 8 с на страницу (один датчик на странице)   <-- ПОДВИНУТЬ (6000-10000)

// Радужный IP-адрес на матрице при загрузке (каждая буква своим цветом)
static void drawBootIpRainbow(const String &ip, uint32_t holdMs) {
  if (!display) return;
  display->setFont(&TomThumb);
  display->setTextWrap(false);

  // Центрируем всю строку по фактической ширине; каждый символ печатаем
  // своим цветом, а шаг/точки расставляет сам шрифт (без ручных сдвигов).
  int16_t bx, by; uint16_t bw, bh;
  display->getTextBounds(ip.c_str(), 0, 12, &bx, &by, &bw, &bh);
  int x0 = (64 - (int)bw) / 2 - bx;
  if (x0 < 2) x0 = 2;

  uint32_t startMs = millis();
  uint16_t phase = 0;
  while (millis() - startMs < holdMs) {
    display->fillScreen(0);
    display->setCursor(x0, 12);
    for (unsigned i = 0; i < ip.length(); i++) {
      uint16_t hue = (uint16_t)((phase + i * 26) % 360);
      uint8_t r, g, b;
      uint8_t region = hue / 60, rem = (hue % 60) * 255 / 60;
      switch (region) {
        case 0: r = 255; g = rem; b = 0; break;
        case 1: r = 255 - rem; g = 255; b = 0; break;
        case 2: r = 0; g = 255; b = rem; break;
        case 3: r = 0; g = 255 - rem; b = 255; break;
        case 4: r = rem; g = 0; b = 255; break;
        default: r = 255; g = 0; b = 255 - rem; break;
      }
      display->setTextColor(display->color565(r, g, b));
      display->print(ip[i]);          // курсор двигает сам шрифт
    }
    display->flipDMABuffer();
    phase = (phase + 6) % 360;
    delay(40);
  }
  display->setFont();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("🚀 PixelHub 75 – Версия 4.9.2 (надёжный расчёт)");

  if (!LittleFS.begin(true)) {
    Serial.println("❌ Ошибка монтирования LittleFS");
    return;
  }
  Serial.println("✅ LittleFS смонтирована");
  pageMemInit();

  loadConfig();
  loadIntegrationsConfig(); // настройки MQTT/Народмона (NVS, не стираются заливкой LittleFS)
  calendarInit();           // события календаря (NVS, тоже не стираются заливкой LittleFS)
  localSensorsInit();       // настройки локальных датчиков по Wi-Fi (NVS)

  initSensor();
  airQualityInit();
  dfpInit();
  alarmDfpInit();
  rtcInit(); // BME280/BMP280 с автоопределением - тоже необязательный
  initOutdoorSensor(); // уличный DS18B20 (1-Wire) - необязательный

  if (!initDisplay()) {
    Serial.println("❌ Display failed!");
    while (1) {
      delay(1000);
      ESP.restart();
    }
  }
  Serial.println("✅ Display ready!");
  showStartupTestPattern(); // тест RGB-каналов на старте (рамки R/G/B)
  showStartupLogoAnimation(); // название проекта: выезд снизу + взрыв на пиксели

  // Пытаемся подключиться к сохранённой сети. Если не получилось -
  // connectWiFiOrStartAP() сам поднимает точку доступа "PixelHub75-Setup"
  // с captive-порталом и возвращает false - тогда дальше setup() не
  // продолжаем (NTP/погода/обычный веб-сервер не нужны, пока не введут
  // рабочую сеть - после сохранения устройство само перезагрузится).
  bool wifiConnected = connectWiFiOrStartAP();

  if (!wifiConnected) {
    Serial.println("📡 Режим настройки Wi-Fi активен - жду ввода сети");
    return;
  }

  String ipStr = WiFi.localIP().toString();
  drawBootIpRainbow(WiFi.localIP().toString(), 6000);
  Serial.print("IP: ");
  Serial.println(ipStr);
  if (initTime()) {
    Serial.println("✅ NTP OK");
    updateTime();
  }
  if (weatherEnabled) {
    if (fetchWeather()) {
      updateFullScrollText();
    } else {
      weatherRetryCount = 1;
      weatherRetryTime = millis() + RETRY_DELAY;
    }
  }
  showWifiResultScreen(true, ipStr); // "Wi-Fi OK" + IP

  // Дальнейшие обновления погоды идут в фоновой задаче (см. loop()) -
  // первый запрос выше остался синхронным, чтобы при старте сразу были данные.
  startWeatherTask();
  startIntegrationsTask(); // MQTT/Народмон: отправка и приём, в фоновой задаче

  applyAutoBrightness();

  scrollOffset = startOffset;
  updateFullScrollText();

  integrationsRegisterWeb(server); // страница /settings/receive (приём данных)
  otaRegisterWeb(server);          // страница /settings/update (обновление прошивки по Wi-Fi)
  calendarRegisterWeb(server);     // страница /settings/calendar (события и напоминания)
  localSensorsRegisterWeb(server); // /api/sensor (приём от датчиков) и страница /settings/sensors
  setupWebServer();

  // Watchdog включаем в самом конце setup(), чтобы долгое подключение
  // к Wi-Fi / NTP / первый запрос погоды не вызвали ложную перезагрузку.
  initWatchdog(20);

  Serial.println("🎯 Готов к работе!");
}

// Страница экрана "комнаты", которая показывается сейчас (вынесена из loop(), чтобы эффект переходов мог вызвать рисование без параметров)
static int roomsPage = 0;
static void drawRoomsCurrent() { drawRoomsScreen(roomsPage); }

void loop() {
  // Пока активна точка доступа настройки Wi-Fi - крутим только DNS
  // captive-портала, остальную логику (часы/радио/будильник/погоду)
  // не запускаем: сеть ещё не настроена, смысла нет.
  if (isWifiSetupApActive()) {
    handleWifiSetupLoop();
    updateApModeScreen();   // радужная волна на имени точки доступа
    delay(10);
    return;
  }

  feedWatchdog();
  airQualityTick();
  rtcTick();
  alarmDfpTick();
  applyHomeSourceOverride();  // «Дома» с Wi-Fi датчика, если выбран

  static unsigned long lastTimeUpdate = 0;
  static int lastDayNum = -1;
  if (millis() - lastTimeUpdate >= 1000) {
    lastTimeUpdate = millis();
    updateTime();
    calendarTick();   // пересобирает строку календаря при смене даты или правках
    colonState = !colonState;
#if !defined(ESP32_CLASSIC)
#endif
    readSensor();
    readOutdoorSensor();
    localSensorsTick();   // датчик по Wi-Fi, назначенный "уличным", подменяет DS18B20, пока данные свежие
    checkBigClockSchedule();
    checkNightSchedule();
#if !defined(ESP32_CLASSIC)
#endif
#if !defined(ESP32_CLASSIC)
#endif

    // Снимок данных для отправки (MQTT/Народмон) + обновление строк приёма:
    // сразу, когда пришли новые значения, и раз в минуту (чтобы устаревшие пропадали)
    integrationsUpdateSnapshot();
    static uint32_t rxSeenVersion = 0;
    static int rxTick = 0;
    uint32_t rxVersion = integrationsRxVersion();
    if (rxVersion != rxSeenVersion || ++rxTick >= 60) {
      rxSeenVersion = rxVersion;
      rxTick = 0;
      updateFullScrollText();
    }

    if (dayNum != lastDayNum) {
      lastDayNum = dayNum;
      updateFullScrollText();
    }

    // Строка "В помещении" зависит от датчика, а не от даты/погоды -
    // обновляем её текст раз в минуту, независимо от остальных триггеров.
    // updateFullScrollText() не трогает scrollOffset/currentScrollLine,
    // так что текущая прокрутка не прерывается.
    static int sensorScrollTick = 0;
    static bool sensorScrollPrev = false;
    bool anyHomeNow = (detectedSensor != SensorType::NONE) || homeSourceActive;
    if (anyHomeNow != sensorScrollPrev) {
      sensorScrollPrev = anyHomeNow;
      sensorScrollTick = 0;
      updateFullScrollText();      // датчик появился/сменился - строка сразу
    } else if (anyHomeNow) {
      sensorScrollTick++;
      if (sensorScrollTick >= 60) {
        sensorScrollTick = 0;
        updateFullScrollText();
      }
    }

    // --- Озвучка через DFPlayer (если подключён) ---
    static int lastChimeHour = -1;
    static bool prevNight = false;
    if (dfpReady()) {
      bool nightNow = isNightModeActive();
      if (nightNow != prevNight) {
        prevNight = nightNow;
        if (dfNightSound) dfpPlayFolder(1, nightNow ? 1 : 2);   // /01/001.mp3 - ночь, /01/002.mp3 - утро
      }
      if (!nightNow && hourlyAnnounceEnabled && minutes == 0 && seconds < 3 && lastChimeHour != hours) {
        lastChimeHour = hours;
        uint8_t folder = (hourlyVoiceMode == 1) ? 22 : ((hourlyVoiceMode == 2) ? 23 : 12);
        dfpPlayFolder(folder, (uint8_t)(hours + 1));   // файлы 000..023 -> 001..024
      }
    }

    applyAutoBrightness();
  }

  static unsigned long lastWeatherCheck = 0;
  if (weatherEnabled) {
    // 1) Забираем результат фонового запроса, если он уже готов. При сбое
    //    глобальные данные не меняются - на экране остаётся последняя
    //    удачная погода, а не нули.
    bool weatherOk = false;
    if (pollWeatherResult(weatherOk)) {
      if (weatherOk) {
        weatherRetryCount = 0;
        updateFullScrollText();
      } else {
        weatherRetryCount++;
        weatherRetryTime = millis() + RETRY_DELAY;
        Serial.printf("Повторная попытка через %d сек\n", RETRY_DELAY/1000);
      }
    }

    // 2) Просим фоновую задачу обновить погоду, когда пора. Сам запрос
    //    loop() не блокирует - анимация идёт без остановок.
    if (WiFi.status() == WL_CONNECTED) {
      unsigned long now = millis();
      bool needUpdate = (now - lastWeatherCheck >= weatherUpdateInterval * 1000UL);
      bool needRetry = (weatherRetryCount > 0 && now >= weatherRetryTime);
      if ((needUpdate || needRetry) && requestWeatherUpdate()) {
        lastWeatherCheck = now;
      }
    } else {
      if (millis() - lastWeatherCheck >= 60000) {
        lastWeatherCheck = millis();
        WiFi.reconnect();
      }
    }
  }

  // Экраны показываются СТРОГО между проходами бегущей строки:
  // как только строка на основном экране дошла до конца (флаг из
  // screen.cpp), переключаемся на первый ВКЛЮЧЁННЫЙ экран из цепочки
  // 1 (прогноз) -> 2 (погода сейчас) -> 3 (дома) -> обратно к часам.
  // Любой из трёх можно выключить независимо от других (как в
  // оригинале) - отключённый экран просто пропускается, остальные
  // идут по цепочке дальше без сбоя.
  static int currentScreen = 0; // 0=часы, 1=прогноз, 2=погода сейчас, 3=дома, 4=комнаты
  static unsigned long screenUntil = 0;

  bool forecastReady = forecastScreenEnabled && weatherEnabled;
  bool weatherNowReady = weatherNowScreenEnabled && weatherEnabled;
  bool homeReady = homeScreenEnabled && (detectedSensor != SensorType::NONE || homeSourceActive);
  bool roomsReady = roomsScreenReady();
  bool airReady = airQualityReady();

  // Возвращает следующий включённый экран строго после "from" по цепочке
  // 1->2->3->4->0(часы). Если после from ничего не включено - возврат к 0.
  auto nextEnabledScreen = [&](int from) -> int {
    if (from < 1 && forecastReady) return 1;
    if (from < 2 && weatherNowReady) return 2;
    if (from < 3 && homeReady) return 3;
    if (from < 4 && roomsReady) return 4;
    if (from < 5 && airReady) return 5;
    return 0;
  };
  auto durationForScreen = [&](int scr) -> unsigned long {
    switch (scr) {
      case 1: return FORECAST_SCREEN_DURATION;
      case 2: return WEATHER_NOW_SCREEN_DURATION;
      case 3: return HOME_SCREEN_DURATION;
      case 4: return ROOMS_SCREEN_DURATION;
      case 5: return 6000;
      default: return 0;
    }
  };

  if (scrollLapCompleted) {
    scrollLapCompleted = false;
    if (currentScreen == 0) {
      int next = nextEnabledScreen(0);
      if (next != 0) {
        currentScreen = next;
        screenUntil = millis() + durationForScreen(next) + fxExtraMs(next);
        if (next == 4) roomsPage = 0;
      }
    }
  }

  // Оверлеи (большие часы, ночь, радио) перекрывают экраны целиком - эффекты переходов тогда сбрасываем
  bool overlayNow = bigClockActive || isNightModeActive();
  if (overlayNow) fxAbort();

  bool timeUp = (currentScreen != 0 && millis() >= screenUntil);
  // Если для этого экрана включено рассыпание на пиксели - ждём, пока оно закончится
  if (timeUp && !overlayNow && fxHoldSwitch(currentScreen)) timeUp = false;

  if (timeUp) {
    if (currentScreen == 4 && roomsReady && roomsPage + 1 < roomsPageCount()) {
      roomsPage++;   // следующая страница экрана "комнаты" (один датчик на странице)
      screenUntil = millis() + ROOMS_SCREEN_DURATION + fxExtraMs(4);
      fxEnter(4);    // новая страница тоже выплывает снизу и рассыпается
    } else {
      int next = nextEnabledScreen(currentScreen);
      currentScreen = next;
      if (next != 0) screenUntil = millis() + durationForScreen(next) + fxExtraMs(next);
      if (next == 4) roomsPage = 0;
    }
  }

  // Сменился экран - запускаем выплывание (если для него включено)
  static int fxPrevScreen = 0;
  if (currentScreen != fxPrevScreen) {
    fxPrevScreen = currentScreen;
    fxEnter(currentScreen);
  }

  display->fillScreen(0);
  if (bigClockActive) {
    // Большие часы ПЕРЕХВАТЫВАЮТ экран целиком - ни бегущая строка,
    // ни прогноз/погода/дома не показываются, пока активны
    drawBigClockScreen();
  } else if (isNightModeActive()) {
    // Ночь/утро - тоже перехватывают экран целиком
    drawNightScreen();
  } else if (currentScreen == 0) {
    drawScreen();
  } else if (currentScreen == 1) {
    fxDrawScreen(1, drawForecastScreen);   // выплывание снизу и рассыпание на пиксели
  } else if (currentScreen == 2) {
    fxDrawScreen(2, drawWeatherNowScreen);
  } else if (currentScreen == 4) {
    fxDrawScreen(4, drawRoomsCurrent);
  } else if (currentScreen == 5) {
    drawAirQualityScreen();
  } else {
    fxDrawScreen(3, drawHomeScreen);
  }
  display->flipDMABuffer();
  delay(20);
}
