#pragma once
// ============================================================
// ГЛОБАЛЬНОЕ СОСТОЯНИЕ ПРОЕКТА
// Все переменные объявлены здесь как extern, а определены
// (с начальными значениями) в src/globals.cpp
// ============================================================
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <ESPAsyncWebServer.h>
#include "pins.h"

// ===== ДИСПЛЕЙ =====
extern MatrixPanel_I2S_DMA *display;

// ===== ТЕКУЩЕЕ ВРЕМЯ =====
extern int hours, minutes, seconds;
extern int dayNum, monthNum, wdayNum;
extern int year;
extern bool colonState;

// ===== ЧАСЫ: НАСТРОЙКИ =====
extern int clockFontIndex;  // индекс в clockFontVariants[7] (часы:минуты)
extern int secFontIndex;    // индекс в secFontVariants[6] (секунды/дата/день недели)
extern String clockColorHex;
extern String secondsColorHex;
extern bool showSeconds;
extern uint16_t clockColor;
extern uint16_t secondsColor;

// ===== ОЗВУЧКА ВРЕМЕНИ КАЖДЫЙ ЧАС =====
// hourlyVoiceMode: 0=женский, 1=мужской, 2=куранты, 3=каминные часы.
// Файлы на SD - по той же схеме папок/номеров, что в оригинале Шилова:
// /22/000.mp3.../023.mp3 (женский), /23/ (мужской), /12/ (куранты),
// /13/ (каминные часы) - имя файла = час (0-23) с ведущими нулями.
extern bool hourlyAnnounceEnabled;
extern int hourlyVoiceMode;
extern uint8_t hourlyVolumePercent; // громкость озвучки, 0..100% (хранится в config.json)

// ===== ОЧЕРЕДЬ СООБЩЕНИЙ (объявлена, но не используется логикой) =====
extern String scrollMessages[MAX_MESSAGES];
extern int messageCount;
extern int currentMessageIndex;

// ===== БЕГУЩАЯ СТРОКА - ОТДЕЛЬНЫЕ СТРОКИ, у каждой свой цвет и своя скорость =====
// Строки: 0=дата, 1=погода сегодня, 2=погода завтра, 3=датчики "дома",
// 4="Сейчас на улице" (уличный датчик DS18B20; пропускается, пока датчик не отвечает)
#define SCROLL_LINE_COUNT 5
extern String scrollLineText[SCROLL_LINE_COUNT];
extern String scrollLineColorHex[SCROLL_LINE_COUNT];
extern uint16_t scrollLineColor[SCROLL_LINE_COUNT];
extern int scrollLineSpeed[SCROLL_LINE_COUNT];   // 1-10, как раньше scrollSpeed
extern bool scrollLineEnabled[SCROLL_LINE_COUNT]; // можно скрыть любую строку целиком
extern int currentScrollLine;                     // какая из 3 строк сейчас едет по экрану
extern int scrollOffset;
extern const int startOffset;
extern const int scrollY;

// ===== ЦВЕТА ДАТЫ =====
extern String dayColorHex;
extern String dateColorHex;
extern String monthColorHex;
extern uint16_t dayColor;
extern uint16_t dateColor;
extern uint16_t monthColor;

// выходные: отдельный цвет дня недели и числа
extern bool weekendColorEnabled;
extern String weekendDayColorHex;
extern String weekendDateColorHex;
extern uint16_t weekendDayColor;
extern uint16_t weekendDateColor;

// ===== НАЗВАНИЯ ДНЕЙ/МЕСЯЦЕВ И КООРДИНАТЫ ЧИСЛА =====
extern const char* dayNames[7];          // короткие (для часового экрана, уже под кодировку шрифта)
extern const char* weekdayNamesFull[7];  // полные, в utf8rus (для бегущей строки)
extern const char* monthNames[12];
extern const int dateNumX[12][2];

// ===== ВЕБ-СЕРВЕР =====
extern AsyncWebServer server;

// ===== WI-FI / ОБЩИЕ НАСТРОЙКИ =====
extern String wifi_ssid;
extern String wifi_password;
extern int timezoneOffset;
extern uint8_t displayBrightness;

// ===== АВТОЯРКОСТЬ ПО ВОСХОДУ/ЗАКАТУ =====
extern bool autoBrightnessEnabled;
extern uint8_t nightBrightness;   // яркость ночью (displayBrightness используется как дневная)
extern int sunriseHour, sunriseMinute;
extern int sunsetHour, sunsetMinute;

// ===== ЭКРАН ПРОГНОЗА (НЕСКОЛЬКО ДНЕЙ) =====
#define FORECAST_DAYS 3        // сколько колонок на экране прогноза
#define FORECAST_DATA_DAYS 5   // на сколько дней вперёд хранятся данные: 0=сегодня, 1=завтра, 2=послезавтра, 3, 4
extern int forecastWeathercode[FORECAST_DATA_DAYS];
extern float forecastTempMin[FORECAST_DATA_DAYS];
extern float forecastTempMax[FORECAST_DATA_DAYS];
extern int forecastDayFontIndex;   // шрифт названий дней на экране прогноза (индекс в secFontVariants)
extern bool forecastScreenEnabled;

// ===== ЭКРАН ПРОГНОЗА - ЦВЕТА ЭЛЕМЕНТОВ (как в оригинале, раздельно) =====
extern String forecastDayColorHex;
extern String forecastTempMinColorHex;
extern String forecastTempMaxColorHex;
extern uint16_t forecastDayColor;
extern uint16_t forecastTempMinColor;
extern uint16_t forecastTempMaxColor;

// ===== ЭКРАНЫ "ПОГОДА СЕЙЧАС" И "ДОМА" - МОЖНО ОТКЛЮЧИТЬ ПО ОТДЕЛЬНОСТИ =====
// (как в оригинале - любой экран можно скрыть независимо от других)
extern bool weatherNowScreenEnabled;
extern bool homeScreenEnabled;

// Устанавливается в true в screen.cpp, когда бегущая строка дошла до
// конца и начала новый круг. loop() в main.cpp считывает и сбрасывает
// этот флаг, чтобы переключиться на экран прогноза строго между
// проходами строки, а не прерывать её на середине.
extern bool scrollLapCompleted;

// ===== ПОГОДА: НАСТРОЙКИ =====
extern float lat;
extern float lon;
extern String gorod;
extern bool weatherEnabled;
extern unsigned long weatherUpdateInterval;
extern unsigned long lastWeatherUpdate;

// ===== ПОГОДА: ИСТОЧНИК ДАННЫХ (МУЛЬТИПРОВАЙДЕР) =====
// weatherSource: 0=Авто (сначала Open-Meteo, при сбое - OpenWeatherMap,
// если задан ключ), 1=Только Open-Meteo, 2=Только OpenWeatherMap.
// Оба провайдера приводят данные к ОДНОЙ И ТОЙ ЖЕ внутренней схеме кодов
// погоды (см. weather.cpp) - forecast.cpp/icons.cpp не видят разницы,
// каким сервисом были получены данные.
extern int weatherSource;
extern String owmApiKey;      // ключ OpenWeatherMap
extern String pirateApiKey;   // ключ Pirate Weather (pirateweather.net) // ключ OpenWeatherMap (openweathermap.org, бесплатный тариф)
// Рантайм (НЕ сохраняется в config.json) - какой сервис реально сработал
// в последний раз, для отображения/отладки на главной странице.
extern String weatherSourceUsed;

// ===== ПОГОДА: ДАННЫЕ (СЕЙЧАС) =====
extern float temperaturedarc;
extern int humiditydarc;
extern float pressuredarc;
extern float windSpeeddarc;
extern String degString;
extern int cloudCoverdarc;
extern String textcode;
extern int location_code;

// ===== ПОГОДА: ДАННЫЕ (ЗАВТРА) =====
extern float temperatureHighdarc;
extern float temperatureLowdarc;
extern int humiditydarc0;
extern float windSpeeddarc0;
extern String degString1;
extern String textcode1;
extern int coded1;

// ===== ЭКРАН "ПОГОДА СЕЙЧАС" - ЦВЕТА ЭЛЕМЕНТОВ (как в оригинале, раздельно) =====
extern String weatherTitleColorHex;      // заголовок "погода"
extern String weatherDateColorHex;       // числа сегодня/завтра
extern String weatherTimeColorHex;       // время h:mm
extern String weatherTempNowColorHex;    // температура "сейчас"
extern String weatherTempTomorrowColorHex; // макс. температура "завтра"
extern uint16_t weatherTitleColor;
extern uint16_t weatherDateColor;
extern uint16_t weatherTimeColor;
extern uint16_t weatherTempNowColor;
extern uint16_t weatherTempTomorrowColor;

// ===== ПОГОДА: ГОТОВЫЕ СТРОКИ ДЛЯ БЕГУЩЕЙ СТРОКИ =====
extern String weatherString;
extern String weatherStringZ;

// ===== ПОГОДА: ПОВТОРНЫЕ ПОПЫТКИ ПРИ ОШИБКЕ =====
extern int weatherRetryCount;
extern unsigned long weatherRetryTime;
extern const unsigned long RETRY_DELAY;

// ===== ДОМАШНИЙ ДАТЧИК (BME280) - экран "дома:" =====
// Поправки к показаниям датчика (применяются в readSensor())
extern float homeTempOffset;         // °C, например -1.5
extern float homePressureOffsetMm;   // мм рт. ст., например -3
extern float homeHumidityOffset;     // %, только BME280
extern bool homeSourceActive;
extern int outdoorPin;      // DATA DS18B20
extern int dfpTxPin;        // DFPlayer TX (платы)
extern int dfpRxPin;        // DFPlayer RX (платы)
extern float airCo2;        // SGP30 eCO2, ppm
extern float airTvoc;       // SGP30 TVOC, ppb
extern bool airValid;
extern int dfVolume;         // громкость DFPlayer 0..30
extern bool dfNightSound;  // играть ночной/утренний звук
extern String airTitleColorHex;
extern String airCo2ColorHex;
extern String airTvocColorHex;
extern String airBarColorHex;
extern uint16_t airTitleColor, airCo2Color, airTvocColor, airBarColor;        // SGP30 найден и данные есть   // значения «Дома» пришли из Wi-Fi датчика
extern float homeTemp;       // °C
extern float homeHumidity;   // %
extern float homePressure;   // гПа (не мм рт.ст.!)

// ===== УЛИЧНЫЙ ДАТЧИК DS18B20 (1-Wire, outdoor_sensor.cpp) =====
extern float outdoorTemp;       // °C, последнее нормальное значение
extern bool outdoorTempValid;   // true, если датчик отвечает и значение разумное

// ===== ЭКРАН "ДОМА:" - ЦВЕТА ЭЛЕМЕНТОВ (как в оригинале, раздельно) =====
extern String homeTitleColorHex;    // заголовок "дома:"
extern String homeTimeColorHex;     // время
extern String homePressureColorHex; // давление
extern String homeTempColorHex;     // температура
extern String homeHumidityColorHex; // влажность
extern uint16_t homeTitleColor;
extern uint16_t homeTimeColor;
extern uint16_t homePressureColor;
extern uint16_t homeTempColor;
extern uint16_t homeHumidityColor;

// ===== БУДИЛЬНИК - своё время на каждый день недели =====
// Индекс 0..6 соответствует tm_wday (0=воскресенье...6=суббота),
// как и wdayNum в остальном проекте.
extern bool alarmEnabled[7];
extern int alarmHour[7];
extern int alarmMinute[7];
extern String alarmSoundPath[7];   // файл на SD, напр. "/alarms/mon.wav"
extern int alarmAutoStopMinutes;   // через сколько минут звук сам заглохнет
extern uint8_t alarmVolumePercent; // громкость будильника, 0..100% (хранится в config.json)

// Рантайм-состояние (не сохраняется в config.json)
extern bool alarmActive;           // сейчас реально звонит
extern unsigned long alarmStartedAt;

// ===== БОЛЬШИЕ ЧАСЫ - отдельный экран-оверлей на весь экран =====
// Пока bigClockActive==true, ВСЁ остальное (бегущая строка, прогноз,
// погода сейчас, дома) не показывается - только время крупным шрифтом.
extern int bigFontIndex;                // индекс в bigFontVariants[13] (шрифт больших часов)
extern String bigClockColorHex;
extern uint16_t bigClockColor;
extern bool bigClockActive;             // рантайм: активен ли режим сейчас

// ===== АНИМАЦИЯ ВЫЕЗДА БОЛЬШИХ ЧАСОВ =====
// bigClockAnimMode - НАСТРОЙКА (хранится в config.json):
// 0=слева, 1=справа, 2=сверху, 3=снизу, 4=хаотично (каждый раз случайно)
extern int bigClockAnimMode;
// Рантайм (не сохраняется) - текущая проигрываемая анимация
extern bool bigClockAnimPlaying;        // сейчас идёт анимация выезда
extern unsigned long bigClockAnimStartMs; // millis() старта текущей анимации
extern int bigClockAnimDir;             // реальное направление ЭТОГО проигрывания (0-3, если mode=4 - выбрано случайно)
extern bool bigClockScheduleEnabled;    // включён ли автозапуск по расписанию
extern int bigClockOnHour, bigClockOnMinute;    // когда включается сам
extern int bigClockOffHour, bigClockOffMinute;  // когда выключается сам

// ===== НОЧНОЙ/УТРЕННИЙ РЕЖИМ - тоже оверлей на весь экран =====
// Активен всё окно nightOnHour..nightOffHour (с оборачиванием через
// полночь, как в оригинале getNreg()/getYtro()). Анимация засыпания/
// пробуждения проигрывается по разу за вход/выход, между ними - тихий
// "спящий" экран.
extern bool nightModeScheduleEnabled;
extern int nightOnHour, nightOnMinute;    // когда наступает ночь
extern int nightOffHour, nightOffMinute;  // когда наступает утро
extern int nightFontIndex;               // шрифт ночных часов (индекс в clockFontVariants, как у основных часов)
extern String nightClockColorHex;        // цвет ночных часов
extern uint16_t nightClockColor;
extern String nightLabelColorHex;        // цвет подписи "улица" на ночном экране
extern uint16_t nightLabelColor;
extern String nightTempColorHex;         // цвет уличной температуры на ночном экране
extern uint16_t nightTempColor;
extern String nightSoundPath;             // "спокойной ночи"
extern String morningSoundPath;           // "доброе утро"

// ===== ВЕБ-РАДИО - расписание вкл/выкл (по аналогии с ночным режимом) =====
extern bool radioScheduleEnabled;
extern int radioOnHour, radioOnMinute;
extern int radioOffHour, radioOffMinute;
extern int radioLastStationIndex;   // на какой станции остановились в прошлый раз
extern uint8_t radioVolumePercent;  // 0..100%, хранится в config.json
extern String radioClockColorHex;   // цвет часов на экране радио
extern String radioTextColorHex;    // цвет бегущей строки на экране радио
extern uint16_t radioClockColor;
extern uint16_t radioTextColor;
extern uint8_t radioViewMode;       // вид экрана радио: 0 - спектр, 1 - VU-метр (два канала)
