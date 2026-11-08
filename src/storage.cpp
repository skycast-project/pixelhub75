#include "storage.h"
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "globals.h"
#include "utils.h"
#include "night_screen.h"

// ============================================================
// РЕЗЕРВНАЯ КОПИЯ config.json ВО ФЛЕШ-ПАМЯТИ (NVS)
// "Upload Filesystem Image" стирает всю файловую систему LittleFS, а вместе с ней
// и /config.json. NVS - отдельный раздел флеш-памяти, заливка data/ его не трогает.
// Поэтому при каждом сохранении настроек копия config.json кладётся ещё и в NVS
// (кусками по CFG_CHUNK байт, с контрольной суммой), а при загрузке, если файла
// нет или он повреждён, настройки берутся из этой копии.
// Копия обновляется только когда настройки реально изменились (экономим ресурс флеш-памяти).
// ============================================================
static const char *CFG_NVS_NS = "cfgbak";
static const size_t CFG_CHUNK = 1000;     // размер одного куска, байт
static const int CFG_MAX_CHUNKS = 8;      // максимум 8000 байт (сейчас config.json около 3-4 КБ)

static uint32_t cfgCrc32(const uint8_t *buf, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  crc = ~crc;
  return crc ? crc : 1u;   // 0 зарезервирован как "копии нет"
}

static void backupConfigToNvs(const String &json) {
  size_t len = json.length();
  if (len == 0 || len > CFG_CHUNK * (size_t)CFG_MAX_CHUNKS) {
    Serial.println("⚠️ Резервная копия настроек не сделана: config.json пустой или слишком большой");
    return;
  }
  uint32_t crc = cfgCrc32((const uint8_t *)json.c_str(), len);

  Preferences p;
  if (!p.begin(CFG_NVS_NS, false)) {
    Serial.println("⚠️ Резервная копия настроек: не удалось открыть NVS");
    return;
  }
  if (p.getUInt("crc", 0) == crc && p.getUInt("len", 0) == (uint32_t)len) {
    p.end();   // настройки не изменились - копия уже актуальна
    return;
  }
  // Сначала сбрасываем контрольную сумму: если питание пропадёт посреди записи,
  // неполная копия не будет принята при загрузке (будет взят основной файл).
  p.putUInt("crc", 0);
  int chunks = (int)((len + CFG_CHUNK - 1) / CFG_CHUNK);
  bool ok = true;
  for (int i = 0; i < chunks && ok; i++) {
    size_t off = (size_t)i * CFG_CHUNK;
    size_t n = (len - off < CFG_CHUNK) ? (len - off) : CFG_CHUNK;
    char key[8];
    snprintf(key, sizeof(key), "c%d", i);
    ok = (p.putBytes(key, json.c_str() + off, n) == n);
  }
  if (ok) {
    p.putUInt("len", (uint32_t)len);
    p.putUInt("crc", crc);
    Serial.println("💾 Резервная копия настроек (NVS) обновлена");
  } else {
    Serial.println("⚠️ Не удалось записать резервную копию настроек в NVS (нет места?). "
                   "Настройки работают, но заливка файловой системы их сотрёт");
  }
  p.end();
}

static bool readConfigFromNvs(String &out) {
  Preferences p;
  if (!p.begin(CFG_NVS_NS, true)) return false;   // раздела ещё нет - копии нет
  uint32_t len = p.getUInt("len", 0);
  uint32_t crc = p.getUInt("crc", 0);
  bool ok = false;
  if (len > 0 && len <= CFG_CHUNK * (size_t)CFG_MAX_CHUNKS && crc != 0) {
    String s;
    s.reserve(len + 1);
    uint8_t buf[CFG_CHUNK];
    int chunks = (int)((len + CFG_CHUNK - 1) / CFG_CHUNK);
    ok = true;
    for (int i = 0; i < chunks && ok; i++) {
      size_t off = (size_t)i * CFG_CHUNK;
      size_t want = (len - off < CFG_CHUNK) ? (len - off) : CFG_CHUNK;
      char key[8];
      snprintf(key, sizeof(key), "c%d", i);
      size_t got = p.getBytes(key, buf, want);
      if (got != want) { ok = false; break; }
      s.concat((const char *)buf, (unsigned int)want);
    }
    if (ok && cfgCrc32((const uint8_t *)s.c_str(), s.length()) != crc) ok = false;
    if (ok) out = s;
  }
  p.end();
  return ok;
}

// Удаляет резервную копию (для "сброса к заводским": после этого удаление /config.json
// снова даёт настройки по умолчанию). Полная очистка флеш-памяти тоже сбрасывает копию.
void clearConfigBackup() {
  Preferences p;
  if (p.begin(CFG_NVS_NS, false)) {
    p.clear();
    p.end();
  }
}

void setDefaultConfig() {
  scrollMessages[0] = "";
  messageCount = 1;
  currentMessageIndex = 0;

  clockFontIndex = 0;
  secFontIndex = 1;
  clockColorHex = "#FF8800";
  secondsColorHex = "#FFFFFF";
  showSeconds = true;

  hourlyAnnounceEnabled = false;
  hourlyVoiceMode = 0;
  hourlyVolumePercent = 70;

  // 4 строки бегущей строки (дата/погода сегодня/погода завтра/датчики
  // "дома") - разные цвета по умолчанию, чтобы визуально отличались сразу
  scrollLineColorHex[0] = "#FFFFFF";
  scrollLineColorHex[1] = "#00CFFF";
  scrollLineColorHex[2] = "#FFCF00";
  scrollLineColorHex[3] = "#7CFF7C";
  scrollLineColorHex[4] = "#FF9F40";
  scrollLineSpeed[0] = 5;
  scrollLineSpeed[1] = 5;
  scrollLineSpeed[2] = 5;
  scrollLineSpeed[3] = 5;
  scrollLineSpeed[4] = 5;
  scrollLineEnabled[0] = true;
  scrollLineEnabled[1] = true;
  scrollLineEnabled[2] = true;
  scrollLineEnabled[3] = true;
  scrollLineEnabled[4] = true;

  dayColorHex = "#FFFFFF";
  dateColorHex = "#FF8800";
  monthColorHex = "#FFFFFF";
  weekendColorEnabled = false;
  weekendDayColorHex = "#FF0000";
  weekendDateColorHex = "#FF0000";

  wifi_ssid = "";
  wifi_password = "";
  timezoneOffset = 3;
  displayBrightness = 200;
  autoBrightnessEnabled = false;
  nightBrightness = 40;

  lat = 55.75;
  lon = 37.62;
  gorod = "Москва";
  weatherEnabled = true;
  weatherUpdateInterval = 600;
  forecastScreenEnabled = true;
  forecastDayFontIndex = 0;
  weatherNowScreenEnabled = true;
  homeScreenEnabled = true;

  weatherSource = 0; // Авто
  owmApiKey = "";
  pirateApiKey = "";

  forecastDayColorHex = "#FFFFFF";
  forecastTempMinColorHex = "#0088FF";
  forecastTempMaxColorHex = "#FF8800";

  weatherTitleColorHex = "#FFFFFF";
  weatherDateColorHex = "#FFFFFF";
  weatherTimeColorHex = "#FFFFFF";
  weatherTempNowColorHex = "#FFFFFF";
  weatherTempTomorrowColorHex = "#FFFFFF";

  homeTitleColorHex = "#FFFFFF";
  homeTimeColorHex = "#FFFFFF";
  radioClockColorHex = "#FFFFFF";
  radioTextColorHex = "#FFFFFF";
  radioViewMode = 0;
  homePressureColorHex = "#FFFFFF";
  homeTempColorHex = "#00AAFF";
  homeHumidityColorHex = "#0088FF";
  homeTempOffset = 0;
  homePressureOffsetMm = 0;
  homeHumidityOffset = 0;

  // Будильник - по умолчанию все дни выключены, время 7:00
  for (int i = 0; i < 7; i++) {
    alarmEnabled[i] = false;
    alarmHour[i] = 7;
    alarmMinute[i] = 0;
  }
  alarmAutoStopMinutes = 3;
  alarmVolumePercent = 70;

  // Большие часы - расписание выключено, окно 22:00-23:00 по умолчанию
  bigFontIndex = 0;
  bigClockColorHex = "#FFFFFF";
  bigClockAnimMode = 0;
  bigClockScheduleEnabled = false;
  bigClockOnHour = 22; bigClockOnMinute = 0;
  bigClockOffHour = 23; bigClockOffMinute = 0;

  // Ночной режим - расписание выключено, окно 23:00-7:00 по умолчанию
  nightModeScheduleEnabled = false;
  nightOnHour = 23; nightOnMinute = 0;
  nightOffHour = 7; nightOffMinute = 0;
  nightFontIndex = 0;
  nightClockColorHex = "#888888";
  nightClockColor = hexToColor(nightClockColorHex);
  nightLabelColorHex = "#888888";
  nightLabelColor = hexToColor(nightLabelColorHex);
  nightTempColorHex = "#888888";
  nightTempColor = hexToColor(nightTempColorHex);
  nightScreenBrightnessPercent = 100;
  nightSoundPath = "/sounds/night.mp3";
  morningSoundPath = "/sounds/morning.mp3";

  clockColor = hexToColor(clockColorHex);
  secondsColor = hexToColor(secondsColorHex);
  for (int i = 0; i < SCROLL_LINE_COUNT; i++) {
    scrollLineColor[i] = hexToColor(scrollLineColorHex[i]);
  }
  dayColor = hexToColor(dayColorHex);
  dateColor = hexToColor(dateColorHex);
  monthColor = hexToColor(monthColorHex);
  weekendDayColor = hexToColor(weekendDayColorHex);
  weekendDateColor = hexToColor(weekendDateColorHex);
    weekendDayColor = hexToColor(weekendDayColorHex);
    weekendDateColor = hexToColor(weekendDateColorHex);
  bigClockColor = hexToColor(bigClockColorHex);

  forecastDayColor = hexToColor(forecastDayColorHex);
  forecastTempMinColor = hexToColor(forecastTempMinColorHex);
  forecastTempMaxColor = hexToColor(forecastTempMaxColorHex);

  weatherTitleColor = hexToColor(weatherTitleColorHex);
  weatherDateColor = hexToColor(weatherDateColorHex);
  weatherTimeColor = hexToColor(weatherTimeColorHex);
  weatherTempNowColor = hexToColor(weatherTempNowColorHex);
  weatherTempTomorrowColor = hexToColor(weatherTempTomorrowColorHex);

  homeTitleColor = hexToColor(homeTitleColorHex);
  homeTimeColor = hexToColor(homeTimeColorHex);
  radioClockColor = hexToColor(radioClockColorHex);
  radioTextColor = hexToColor(radioTextColorHex);
  homePressureColor = hexToColor(homePressureColorHex);
  homeTempColor = hexToColor(homeTempColorHex);
  homeHumidityColor = hexToColor(homeHumidityColorHex);

  saveConfig();
}

void loadConfig() {
  JsonDocument doc;
  bool loaded = false;

  File file = LittleFS.open("/config.json", "r");
  if (file) {
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (!error) loaded = true;
    else Serial.println("Ошибка парсинга config.json");
  } else {
    Serial.println("config.json не найден");
  }

  // Файла нет или он повреждён (например, после "Upload Filesystem Image") -
  // пробуем взять настройки из резервной копии во флеш-памяти (NVS)
  if (!loaded) {
    String backup;
    if (readConfigFromNvs(backup)) {
      doc.clear();
      if (deserializeJson(doc, backup) == DeserializationError::Ok) {
        loaded = true;
        Serial.println("♻️ Настройки восстановлены из резервной копии во флеш-памяти (NVS)");
      }
    }
  }

  if (!loaded) {
    Serial.println("Резервной копии нет, создаём настройки по умолчанию");
    setDefaultConfig();
    return;
  }

  clockFontIndex = doc["clockFontIndex"] | 0;
  secFontIndex = doc["secFontIndex"] | 1;
  clockColorHex = doc["clockColor"] | "#FF8800";
  secondsColorHex = doc["secondsColor"] | "#FFFFFF";
  showSeconds = doc["showSeconds"] | true;

  hourlyAnnounceEnabled = doc["hourlyAnnounceEnabled"] | false;
  hourlyVoiceMode = doc["hourlyVoiceMode"] | 0;
  {
    int hv = doc["hourlyVolumePercent"] | 70;
    hourlyVolumePercent = (uint8_t)constrain(hv, 0, 100);
  }

  scrollMessages[0] = "";
  messageCount = 1;
  currentMessageIndex = 0;

  // 4 строки - читаем массивом из scrollLines[]; если в старом
  // config.json этого массива меньше (обновление с 3 строк на 4, или
  // совсем старое - с одной) - используются значения по умолчанию ниже
  JsonArray scrollLinesArr = doc["scrollLines"];
  const char* defaultColors[SCROLL_LINE_COUNT] = {"#FFFFFF", "#00CFFF", "#FFCF00", "#7CFF7C", "#FF9F40"};
  for (int i = 0; i < SCROLL_LINE_COUNT; i++) {
    if (scrollLinesArr && i < (int)scrollLinesArr.size()) {
      scrollLineColorHex[i] = scrollLinesArr[i]["color"] | defaultColors[i];
      scrollLineSpeed[i] = scrollLinesArr[i]["speed"] | 5;
      scrollLineEnabled[i] = scrollLinesArr[i]["enabled"] | true;
    } else {
      scrollLineColorHex[i] = defaultColors[i];
      scrollLineSpeed[i] = 5;
      scrollLineEnabled[i] = true;
    }
  }

  dayColorHex = doc["dayColor"] | "#FFFFFF";
  dateColorHex = doc["dateColor"] | "#FF8800";
  monthColorHex = doc["monthColor"] | "#FFFFFF";
  weekendColorEnabled = doc["weekendColorEnabled"] | false;
  weekendDayColorHex = doc["weekendDayColor"] | "#FF0000";
  weekendDateColorHex = doc["weekendDateColor"] | "#FF0000";

  wifi_ssid = doc["wifi_ssid"] | "";
  wifi_password = doc["wifi_password"] | "";
  timezoneOffset = doc["timezoneOffset"] | 3;
  displayBrightness = doc["brightness"] | 200;
  autoBrightnessEnabled = doc["autoBrightnessEnabled"] | false;
  nightBrightness = doc["nightBrightness"] | 40;

  lat = doc["lat"] | 55.75f;
  lon = doc["lon"] | 37.62f;
  gorod = doc["gorod"] | "Москва";
  weatherEnabled = doc["weatherEnabled"] | true;
  weatherUpdateInterval = doc["weatherUpdateInterval"] | 600;
  forecastScreenEnabled = doc["forecastScreenEnabled"] | true;
  forecastDayFontIndex = doc["forecastDayFontIndex"] | 0;
  weatherNowScreenEnabled = doc["weatherNowScreenEnabled"] | true;
  homeScreenEnabled = doc["homeScreenEnabled"] | true;

  weatherSource = doc["weatherSource"] | 0;
  owmApiKey = doc["owmApiKey"] | "";
  pirateApiKey = doc["pirateApiKey"] | "";

  forecastDayColorHex = doc["forecastDayColor"] | "#FFFFFF";
  forecastTempMinColorHex = doc["forecastTempMinColor"] | "#0088FF";
  forecastTempMaxColorHex = doc["forecastTempMaxColor"] | "#FF8800";

  weatherTitleColorHex = doc["weatherTitleColor"] | "#FFFFFF";
  airTitleColorHex = doc["airTitleColor"] | "#00C8FF";
  airCo2ColorHex = doc["airCo2Color"] | "#00DC78";
  airTvocColorHex = doc["airTvocColor"] | "#00A0FF";
  airBarColorHex = doc["airBarColor"] | "#00A0FF";
  airTitleColor = hexToColor(airTitleColorHex);
  airCo2Color = hexToColor(airCo2ColorHex);
  airTvocColor = hexToColor(airTvocColorHex);
  airBarColor = hexToColor(airBarColorHex);
  dfVolume = constrain((int)(doc["dfVolume"] | 20), 0, 30);
  dfNightSound = (doc["dfNight"] | 1) != 0;
  outdoorPin = constrain((int)(doc["outdoorPin"] | 3), 0, 39);
  dfpTxPin = constrain((int)(doc["dfpTx"] | 19), 0, 39);
  dfpRxPin = constrain((int)(doc["dfpRx"] | 35), 0, 39);
  weatherDateColorHex = doc["weatherDateColor"] | "#FFFFFF";
  weatherTimeColorHex = doc["weatherTimeColor"] | "#FFFFFF";
  weatherTempNowColorHex = doc["weatherTempNowColor"] | "#FFFFFF";
  weatherTempTomorrowColorHex = doc["weatherTempTomorrowColor"] | "#FFFFFF";

  homeTitleColorHex = doc["homeTitleColor"] | "#FFFFFF";
  homeTimeColorHex = doc["homeTimeColor"] | "#FFFFFF";
  radioClockColorHex = doc["radioClockColor"] | "#FFFFFF";
  radioTextColorHex = doc["radioTextColor"] | "#FFFFFF";
  radioViewMode = (uint8_t)constrain((int)(doc["radioViewMode"] | 0), 0, 1);
  homePressureColorHex = doc["homePressureColor"] | "#FFFFFF";
  homeTempColorHex = doc["homeTempColor"] | "#00AAFF";
  homeHumidityColorHex = doc["homeHumidityColor"] | "#0088FF";
  homeTempOffset = doc["homeTempOffset"] | 0.0f;
  homePressureOffsetMm = doc["homePressureOffsetMm"] | 0.0f;
  homeHumidityOffset = doc["homeHumidityOffset"] | 0.0f;

  // Будильник - массив из 7 объектов (по дню недели, индекс 0=вс..6=сб)
  JsonArray alarmsArr = doc["alarms"];
  for (int i = 0; i < 7; i++) {
    if (alarmsArr && i < (int)alarmsArr.size()) {
      alarmEnabled[i] = alarmsArr[i]["enabled"] | false;
      alarmHour[i] = alarmsArr[i]["hour"] | 7;
      alarmMinute[i] = alarmsArr[i]["minute"] | 0;
      alarmSoundPath[i] = alarmsArr[i]["sound"] | alarmSoundPath[i];
    }
  }
  alarmAutoStopMinutes = doc["alarmAutoStopMinutes"] | 3;
  alarmVolumePercent = doc["alarmVolumePercent"] | 70;

  // Большие часы - расписание
  bigFontIndex = doc["bigFontIndex"] | 0;
  bigClockColorHex = doc["bigClockColor"] | "#FFFFFF";
  bigClockAnimMode = doc["bigClockAnimMode"] | 0;
  bigClockScheduleEnabled = doc["bigClockScheduleEnabled"] | false;
  bigClockOnHour = doc["bigClockOnHour"] | 22;
  bigClockOnMinute = doc["bigClockOnMinute"] | 0;
  bigClockOffHour = doc["bigClockOffHour"] | 23;
  bigClockOffMinute = doc["bigClockOffMinute"] | 0;

  nightModeScheduleEnabled = doc["nightModeScheduleEnabled"] | false;
  nightOnHour = doc["nightOnHour"] | 23;
  nightOnMinute = doc["nightOnMinute"] | 0;
  nightOffHour = doc["nightOffHour"] | 7;
  nightOffMinute = doc["nightOffMinute"] | 0;
  nightSoundPath = doc["nightSoundPath"] | nightSoundPath;
  morningSoundPath = doc["morningSoundPath"] | morningSoundPath;
  nightFontIndex = doc["nightFontIndex"] | 0;
  nightClockColorHex = doc["nightClockColor"] | "#888888";
  nightClockColor = hexToColor(nightClockColorHex);
  nightLabelColorHex = doc["nightLabelColor"] | "#888888";
  nightLabelColor = hexToColor(nightLabelColorHex);
  nightTempColorHex = doc["nightTempColor"] | "#888888";
  nightTempColor = hexToColor(nightTempColorHex);
  {
    int nb = doc["nightScreenBrightness"] | 100;
    nightScreenBrightnessPercent = (uint8_t)constrain(nb, 10, 100);
  }

  // Веб-радио - расписание/громкость/последняя станция
  radioScheduleEnabled = doc["radioScheduleEnabled"] | false;
  radioOnHour = doc["radioOnHour"] | 8;
  radioOnMinute = doc["radioOnMinute"] | 0;
  radioOffHour = doc["radioOffHour"] | 22;
  radioOffMinute = doc["radioOffMinute"] | 0;
  radioLastStationIndex = doc["radioLastStationIndex"] | -1;
  radioVolumePercent = doc["radioVolumePercent"] | 60;

  clockColor = hexToColor(clockColorHex);
  secondsColor = hexToColor(secondsColorHex);
  for (int i = 0; i < SCROLL_LINE_COUNT; i++) {
    scrollLineColor[i] = hexToColor(scrollLineColorHex[i]);
  }
  dayColor = hexToColor(dayColorHex);
  dateColor = hexToColor(dateColorHex);
  monthColor = hexToColor(monthColorHex);
  bigClockColor = hexToColor(bigClockColorHex);

  forecastDayColor = hexToColor(forecastDayColorHex);
  forecastTempMinColor = hexToColor(forecastTempMinColorHex);
  forecastTempMaxColor = hexToColor(forecastTempMaxColorHex);

  weatherTitleColor = hexToColor(weatherTitleColorHex);
  weatherDateColor = hexToColor(weatherDateColorHex);
  weatherTimeColor = hexToColor(weatherTimeColorHex);
  weatherTempNowColor = hexToColor(weatherTempNowColorHex);
  weatherTempTomorrowColor = hexToColor(weatherTempTomorrowColorHex);

  homeTitleColor = hexToColor(homeTitleColorHex);
  homeTimeColor = hexToColor(homeTimeColorHex);
  radioClockColor = hexToColor(radioClockColorHex);
  radioTextColor = hexToColor(radioTextColorHex);
  homePressureColor = hexToColor(homePressureColorHex);
  homeTempColor = hexToColor(homeTempColorHex);
  homeHumidityColor = hexToColor(homeHumidityColorHex);

  saveConfig();
  Serial.println("✅ Конфигурация загружена");
}

void saveConfig() {
  JsonDocument doc;
  doc["clockFontIndex"] = clockFontIndex;
  doc["secFontIndex"] = secFontIndex;
  doc["clockColor"] = clockColorHex;
  doc["secondsColor"] = secondsColorHex;
  doc["showSeconds"] = showSeconds;

  doc["hourlyAnnounceEnabled"] = hourlyAnnounceEnabled;
  doc["hourlyVoiceMode"] = hourlyVoiceMode;
  doc["hourlyVolumePercent"] = hourlyVolumePercent;

  doc["scrollMessages"] = "";

  JsonArray scrollLinesArr = doc["scrollLines"].to<JsonArray>();
  for (int i = 0; i < SCROLL_LINE_COUNT; i++) {
    JsonObject line = scrollLinesArr.add<JsonObject>();
    line["color"] = scrollLineColorHex[i];
    line["speed"] = scrollLineSpeed[i];
    line["enabled"] = scrollLineEnabled[i];
  }

  doc["dayColor"] = dayColorHex;
  doc["dateColor"] = dateColorHex;
  doc["monthColor"] = monthColorHex;
  doc["weekendColorEnabled"] = weekendColorEnabled;
  doc["weekendDayColor"] = weekendDayColorHex;
  doc["weekendDateColor"] = weekendDateColorHex;

  doc["wifi_ssid"] = wifi_ssid;
  doc["wifi_password"] = wifi_password;
  doc["timezoneOffset"] = timezoneOffset;
  doc["brightness"] = displayBrightness;
  doc["autoBrightnessEnabled"] = autoBrightnessEnabled;
  doc["nightBrightness"] = nightBrightness;

  doc["lat"] = lat;
  doc["lon"] = lon;
  doc["gorod"] = gorod;
  doc["weatherEnabled"] = weatherEnabled;
  doc["weatherUpdateInterval"] = weatherUpdateInterval;
  doc["forecastScreenEnabled"] = forecastScreenEnabled;
  doc["forecastDayFontIndex"] = forecastDayFontIndex;
  doc["weatherNowScreenEnabled"] = weatherNowScreenEnabled;
  doc["homeScreenEnabled"] = homeScreenEnabled;

  doc["weatherSource"] = weatherSource;
  doc["owmApiKey"] = owmApiKey;
  doc["pirateApiKey"] = pirateApiKey;

  doc["forecastDayColor"] = forecastDayColorHex;
  doc["forecastTempMinColor"] = forecastTempMinColorHex;
  doc["forecastTempMaxColor"] = forecastTempMaxColorHex;

  doc["weatherTitleColor"] = weatherTitleColorHex;
  doc["airTitleColor"] = airTitleColorHex;
  doc["airCo2Color"] = airCo2ColorHex;
  doc["airTvocColor"] = airTvocColorHex;
  doc["airBarColor"] = airBarColorHex;
  doc["dfVolume"] = dfVolume;
  doc["dfNight"] = dfNightSound ? 1 : 0;
  doc["outdoorPin"] = outdoorPin;
  doc["dfpTx"] = dfpTxPin;
  doc["dfpRx"] = dfpRxPin;
  doc["weatherDateColor"] = weatherDateColorHex;
  doc["weatherTimeColor"] = weatherTimeColorHex;
  doc["weatherTempNowColor"] = weatherTempNowColorHex;
  doc["weatherTempTomorrowColor"] = weatherTempTomorrowColorHex;

  doc["homeTitleColor"] = homeTitleColorHex;
  doc["homeTimeColor"] = homeTimeColorHex;
  doc["radioClockColor"] = radioClockColorHex;
  doc["radioTextColor"] = radioTextColorHex;
  doc["radioViewMode"] = radioViewMode;
  doc["homePressureColor"] = homePressureColorHex;
  doc["homeTempColor"] = homeTempColorHex;
  doc["homeHumidityColor"] = homeHumidityColorHex;
  doc["homeTempOffset"] = homeTempOffset;
  doc["homePressureOffsetMm"] = homePressureOffsetMm;
  doc["homeHumidityOffset"] = homeHumidityOffset;

  JsonArray alarmsArr = doc["alarms"].to<JsonArray>();
  for (int i = 0; i < 7; i++) {
    JsonObject a = alarmsArr.add<JsonObject>();
    a["enabled"] = alarmEnabled[i];
    a["hour"] = alarmHour[i];
    a["minute"] = alarmMinute[i];
    a["sound"] = alarmSoundPath[i];
  }
  doc["alarmAutoStopMinutes"] = alarmAutoStopMinutes;
  doc["alarmVolumePercent"] = alarmVolumePercent;

  doc["bigFontIndex"] = bigFontIndex;
  doc["bigClockColor"] = bigClockColorHex;
  doc["bigClockAnimMode"] = bigClockAnimMode;
  doc["bigClockScheduleEnabled"] = bigClockScheduleEnabled;
  doc["bigClockOnHour"] = bigClockOnHour;
  doc["bigClockOnMinute"] = bigClockOnMinute;
  doc["bigClockOffHour"] = bigClockOffHour;
  doc["bigClockOffMinute"] = bigClockOffMinute;

  doc["nightModeScheduleEnabled"] = nightModeScheduleEnabled;
  doc["nightOnHour"] = nightOnHour;
  doc["nightOnMinute"] = nightOnMinute;
  doc["nightOffHour"] = nightOffHour;
  doc["nightOffMinute"] = nightOffMinute;
  doc["nightSoundPath"] = nightSoundPath;
  doc["morningSoundPath"] = morningSoundPath;
  doc["nightFontIndex"] = nightFontIndex;
  doc["nightClockColor"] = nightClockColorHex;
  doc["nightLabelColor"] = nightLabelColorHex;
  doc["nightTempColor"] = nightTempColorHex;
  doc["nightScreenBrightness"] = nightScreenBrightnessPercent;

  doc["radioScheduleEnabled"] = radioScheduleEnabled;
  doc["radioOnHour"] = radioOnHour;
  doc["radioOnMinute"] = radioOnMinute;
  doc["radioOffHour"] = radioOffHour;
  doc["radioOffMinute"] = radioOffMinute;
  doc["radioLastStationIndex"] = radioLastStationIndex;
  doc["radioVolumePercent"] = radioVolumePercent;

  String json;
  serializeJson(doc, json);

  File file = LittleFS.open("/config.json", "w");
  if (!file) {
    Serial.println("❌ Ошибка открытия config.json для записи");
  } else {
    if (file.print(json) != json.length()) {
      Serial.println("❌ Ошибка записи JSON");
    }
    file.close();
  }

  // Копия во флеш-памяти (NVS) - переживает заливку файловой системы.
  // Делаем даже если запись файла не удалась.
  backupConfigToNvs(json);
  Serial.println("✅ Настройки сохранены");
}
