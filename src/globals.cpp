#include "globals.h"

// ===== ДИСПЛЕЙ =====
MatrixPanel_I2S_DMA *display = nullptr;

// ===== ТЕКУЩЕЕ ВРЕМЯ =====
int hours = 0, minutes = 0, seconds = 0;
int dayNum = 1, monthNum = 0, wdayNum = 0;
int year = 2026;
bool colonState = true;

// ===== ЧАСЫ: НАСТРОЙКИ =====
int clockFontIndex = FONT_INDEX_CLOCK;
int secFontIndex = 1;  // как в оригинале fss=1 -> kongtext4pt7b
String clockColorHex = "#FF8800";
String secondsColorHex = "#FFFFFF";
bool showSeconds = true;
uint16_t clockColor = 0xF880;
uint16_t secondsColor = 0xFFFF;

// ===== ОЗВУЧКА ВРЕМЕНИ КАЖДЫЙ ЧАС =====
bool hourlyAnnounceEnabled = false;
int hourlyVoiceMode = 0;  // по умолчанию женский голос
uint8_t hourlyVolumePercent = 70;

// ===== ОЧЕРЕДЬ СООБЩЕНИЙ (не используется) =====
String scrollMessages[MAX_MESSAGES];
int messageCount = 0;
int currentMessageIndex = 0;

// ===== БЕГУЩАЯ СТРОКА - 3 отдельные строки =====
String scrollLineText[SCROLL_LINE_COUNT] = {"", "", "", "", ""};
String scrollLineColorHex[SCROLL_LINE_COUNT] = {"#FFFFFF", "#00CFFF", "#FFCF00", "#7CFF7C", "#FF9F40"};
uint16_t scrollLineColor[SCROLL_LINE_COUNT] = {0xFFFF, 0x07FF, 0xFFE0, 0x7FEE, 0xFD20};
int scrollLineSpeed[SCROLL_LINE_COUNT] = {5, 5, 5, 5, 5};
bool scrollLineEnabled[SCROLL_LINE_COUNT] = {true, true, true, true, true};
int currentScrollLine = 0;
int scrollOffset = 0;
const int startOffset = PANEL_W;
const int scrollY = 29;

// ===== ЦВЕТА ДАТЫ =====
String dayColorHex = "#FFFFFF";
String dateColorHex = "#FF8800";
String monthColorHex = "#FFFFFF";
uint16_t dayColor = 0xFFFF;
uint16_t dateColor = 0xF880;
uint16_t monthColor = 0xFFFF;

// ===== WEEKEND COLORS =====
bool weekendColorEnabled = false;
String weekendDayColorHex = "#FF0000";
String weekendDateColorHex = "#FF0000";
uint16_t weekendDayColor = 0xF800;
uint16_t weekendDateColor = 0xF800;

// ===== НАЗВАНИЯ ДНЕЙ/МЕСЯЦЕВ И КООРДИНАТЫ ЧИСЛА =====
const char* dayNames[7] = {"Bc", "Kn", "Bt", "Cp", "Ht", "Kt", "Cb"};
// Порядок соответствует tm_wday (0 = воскресенье ... 6 = суббота)
const char* weekdayNamesFull[7] = {
  "воскресенье", "понедельник", "вторник", "среда", "четверг", "пятница", "суббота"
};
const char* monthNames[12] = {
  "января", "февраля", "марта", "апреля", "мая", "июня",
  "июля", "августа", "сентября", "октября", "ноября", "декабря"
};
const int dateNumX[12][2] = {
  {9, 6}, {5, 2}, {11, 8}, {9, 6}, {17, 14}, {14, 11},
  {14, 11}, {6, 3}, {3, 0}, {6, 3}, {9, 6}, {6, 3}
};

// ===== ВЕБ-СЕРВЕР =====
AsyncWebServer server(80);

// ===== WI-FI / ОБЩИЕ НАСТРОЙКИ =====
String wifi_ssid = "";
String wifi_password = "";
int timezoneOffset = 3;
uint8_t displayBrightness = 200;

// ===== АВТОЯРКОСТЬ ПО ВОСХОДУ/ЗАКАТУ =====
bool autoBrightnessEnabled = false;
uint8_t nightBrightness = 40;
int sunriseHour = 6, sunriseMinute = 0;
int sunsetHour = 21, sunsetMinute = 0;

// ===== ЭКРАН ПРОГНОЗА (НЕСКОЛЬКО ДНЕЙ) =====
int forecastWeathercode[FORECAST_DATA_DAYS] = {800, 800, 800, 800, 800};
float forecastTempMin[FORECAST_DATA_DAYS] = {0, 0, 0, 0, 0};
float forecastTempMax[FORECAST_DATA_DAYS] = {0, 0, 0, 0, 0};
int forecastDayFontIndex = 0;   // 0 = "тонкий1"
bool forecastScreenEnabled = true;
bool weatherNowScreenEnabled = true;
bool homeScreenEnabled = true;
bool scrollLapCompleted = false;

// ===== ЭКРАН ПРОГНОЗА - ЦВЕТА (дефолты = то, что было зашито раньше) =====
String forecastDayColorHex = "#FFFFFF";
String forecastTempMinColorHex = "#0088FF";  // color444(0,8,15)
String forecastTempMaxColorHex = "#FF8800";  // color444(15,8,0)
uint16_t forecastDayColor = 0xFFFF;
uint16_t forecastTempMinColor = 0xFFFF;
uint16_t forecastTempMaxColor = 0xF880;

// ===== ПОГОДА: НАСТРОЙКИ =====
float lat = 55.75;
float lon = 37.62;
String gorod = "Москве";
bool weatherEnabled = true;
unsigned long weatherUpdateInterval = 600;
unsigned long lastWeatherUpdate = 0;

// ===== ПОГОДА: ДАННЫЕ (СЕЙЧАС) =====
float temperaturedarc = 0;
int humiditydarc = 0;
float pressuredarc = 0;
float windSpeeddarc = 0;
String degString = "";
int cloudCoverdarc = 0;
String textcode = "";
int location_code = 800;

// ===== ПОГОДА: ДАННЫЕ (ЗАВТРА) =====
float temperatureHighdarc = 0;
float temperatureLowdarc = 0;
int humiditydarc0 = 0;
float windSpeeddarc0 = 0;
String degString1 = "";
String textcode1 = "";
int coded1 = 800;

// ===== ЭКРАН "ПОГОДА СЕЙЧАС" - ЦВЕТА (дефолт = белый, как сейчас в коде) =====
String weatherTitleColorHex = "#FFFFFF";
String weatherDateColorHex = "#FFFFFF";
String weatherTimeColorHex = "#FFFFFF";
String weatherTempNowColorHex = "#FFFFFF";
String weatherTempTomorrowColorHex = "#FFFFFF";
uint16_t weatherTitleColor = 0xFFFF;
uint16_t weatherDateColor = 0xFFFF;
uint16_t weatherTimeColor = 0xFFFF;
uint16_t weatherTempNowColor = 0xFFFF;
uint16_t weatherTempTomorrowColor = 0xFFFF;

// ===== ПОГОДА: ГОТОВЫЕ СТРОКИ =====
String weatherString = "";
String weatherStringZ = "";

// ===== ПОГОДА: ПОВТОРНЫЕ ПОПЫТКИ =====
int weatherRetryCount = 0;
unsigned long weatherRetryTime = 0;
const unsigned long RETRY_DELAY = 60000;

// ===== ДОМАШНИЙ ДАТЧИК (BME280) =====
bool homeSourceActive = false;
int outdoorPin = 3;
int dfpTxPin = 19;
int dfpRxPin = 35;
float airCo2 = 0;
float airTvoc = 0;
bool airValid = false;
int dfVolume = 20;
bool dfNightSound = true;
String airTitleColorHex = "#00C8FF";
String airCo2ColorHex = "#00DC78";
String airTvocColorHex = "#00A0FF";
String airBarColorHex = "#00A0FF";
uint16_t airTitleColor = 0x07FF;
uint16_t airCo2Color = 0x07E0;
uint16_t airTvocColor = 0x001F;
uint16_t airBarColor = 0x001F;
float homeTempOffset = 0;
float homePressureOffsetMm = 0;
float homeHumidityOffset = 0;
float homeTemp = 0;
float homeHumidity = 0;
float homePressure = 0;

// ===== УЛИЧНЫЙ ДАТЧИК DS18B20 =====
float outdoorTemp = 0;
bool outdoorTempValid = false;

// ===== ЭКРАН "ДОМА:" - ЦВЕТА (дефолты = то, что было зашито раньше) =====
String homeTitleColorHex = "#FFFFFF";
String homeTimeColorHex = "#FFFFFF";
String homePressureColorHex = "#FFFFFF";
String homeTempColorHex = "#00AAFF";     // color444(0,10,15)
String homeHumidityColorHex = "#0088FF"; // color444(0,8,15)
uint16_t homeTitleColor = 0xFFFF;
uint16_t homeTimeColor = 0xFFFF;
uint16_t homePressureColor = 0xFFFF;
uint16_t homeTempColor = 0xFFFF;
uint16_t homeHumidityColor = 0xFFFF;

// ===== БУДИЛЬНИК =====
bool alarmEnabled[7] = {false, false, false, false, false, false, false};
int alarmHour[7] = {7, 7, 7, 7, 7, 7, 7};
int alarmMinute[7] = {0, 0, 0, 0, 0, 0, 0};
String alarmSoundPath[7] = {
  "/alarms/sun.mp3", "/alarms/mon.mp3", "/alarms/tue.mp3", "/alarms/wed.mp3",
  "/alarms/thu.mp3", "/alarms/fri.mp3", "/alarms/sat.mp3"
};
int alarmAutoStopMinutes = 3;
uint8_t alarmVolumePercent = 70;

bool alarmActive = false;
unsigned long alarmStartedAt = 0;

// ===== БОЛЬШИЕ ЧАСЫ =====
int bigFontIndex = 0;  // как в оригинале font=0 -> FreeSansOblique18pt7a
String bigClockColorHex = "#FFFFFF";
uint16_t bigClockColor = 0xFFFF;
bool bigClockActive = false;

// ===== АНИМАЦИЯ ВЫЕЗДА БОЛЬШИХ ЧАСОВ =====
int bigClockAnimMode = 0;   // по умолчанию "слева"
bool bigClockAnimPlaying = false;
unsigned long bigClockAnimStartMs = 0;
int bigClockAnimDir = 0;
bool bigClockScheduleEnabled = false;
int bigClockOnHour = 22, bigClockOnMinute = 0;
int bigClockOffHour = 23, bigClockOffMinute = 0;

// ===== НОЧНОЙ/УТРЕННИЙ РЕЖИМ =====
bool nightModeScheduleEnabled = false;
int nightOnHour = 23, nightOnMinute = 0;
int nightOffHour = 7, nightOffMinute = 0;
int nightFontIndex = 0;
String nightClockColorHex = "#888888";
uint16_t nightClockColor = 0x8C51;
String nightLabelColorHex = "#888888";
uint16_t nightLabelColor = 0x8C51;
String nightTempColorHex = "#888888";
uint16_t nightTempColor = 0x8C51;
String nightSoundPath = "/sounds/night.mp3";
String morningSoundPath = "/sounds/morning.mp3";

bool radioScheduleEnabled = false;
int radioOnHour = 8, radioOnMinute = 0;
int radioOffHour = 22, radioOffMinute = 0;
int radioLastStationIndex = -1;
uint8_t radioVolumePercent = 60;
String radioClockColorHex = "#FFFFFF";
String radioTextColorHex = "#FFFFFF";
uint16_t radioClockColor = 0xFFFF;
uint16_t radioTextColor = 0xFFFF;
uint8_t radioViewMode = 0;
int weatherSource = 0;
String owmApiKey = "";
String pirateApiKey = "";
String weatherSourceUsed = "";