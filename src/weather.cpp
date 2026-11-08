#include "weather.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>
#include <atomic>
#include "globals.h"
#include "wind.h"

// ============================================================
// АРХИТЕКТУРА
// Скачивание (медленное, зависит от сети) выполняется в отдельной
// FreeRTOS-задаче и складывает СЫРОЙ текст ответа в weatherTaskRaw.
// Разбор JSON и запись в глобальные переменные (быстро, миллисекунды)
// делает основной поток в pollWeatherResult(). Так глобальные String
// никогда не трогаются из двух потоков одновременно, а при неудачном
// запросе глобальные данные вообще не меняются (остаются последние
// удачные).
// ============================================================

// Достаёт часы/минуты из строки вида "2026-08-23T06:12" (формат
// sunrise/sunset у Open-Meteo при timezone=auto - локальное время).
static void parseTimeFromISO(const String &iso, int &hourOut, int &minuteOut) {
  int tIndex = iso.indexOf('T');
  if (tIndex < 0 || (int)iso.length() < tIndex + 6) return;
  hourOut = iso.substring(tIndex + 1, tIndex + 3).toInt();
  minuteOut = iso.substring(tIndex + 4, tIndex + 6).toInt();
}

// ============================================================
// ЕДИНАЯ СХЕМА КОДОВ ПОГОДЫ (формат Open-Meteo: 0,1,2,3,45,48,51...99)
// Оба провайдера приводят свои данные именно к этой схеме, поэтому
// forecast.cpp/icons.cpp одинаково работают независимо от источника.
// ============================================================

static void weathercodeToNow(int weathercode, String &textOut, int &codeOut) {
  if (weathercode == 0)  { textOut = "Ясно"; codeOut = 800; }
  else if (weathercode == 1)  { textOut = "Преимущественно ясно"; codeOut = 801; }
  else if (weathercode == 2)  { textOut = "Переменная облачность"; codeOut = 802; }
  else if (weathercode == 3)  { textOut = "Плотная облачность"; codeOut = 804; }
  else if (weathercode == 45) { textOut = "Туман"; codeOut = 701; }
  else if (weathercode == 48) { textOut = "Изморозь"; codeOut = 600; }
  else if (weathercode == 51 || weathercode == 53 || weathercode == 55) { textOut = "Морось"; codeOut = 300; }
  else if (weathercode == 56 || weathercode == 57) { textOut = "Ледяная морось"; codeOut = 300; }
  else if (weathercode == 61) { textOut = "Слабый дождь"; codeOut = 300; }
  else if (weathercode == 63) { textOut = "Умеренный дождь"; codeOut = 300; }
  else if (weathercode == 65) { textOut = "Сильный дождь"; codeOut = 300; }
  else if (weathercode == 66 || weathercode == 67) { textOut = "Ледяной дождь"; codeOut = 300; }
  else if (weathercode == 71 || weathercode == 73 || weathercode == 75) { textOut = "Снегопад"; codeOut = 601; }
  else if (weathercode == 77) { textOut = "Снежные зерна"; codeOut = 601; }
  else if (weathercode == 80 || weathercode == 81 || weathercode == 82) { textOut = "Ливень"; codeOut = 313; }
  else if (weathercode == 85 || weathercode == 86) { textOut = "Снег"; codeOut = 601; }
  else if (weathercode == 95) { textOut = "Гроза"; codeOut = 200; }
  else if (weathercode == 96 || weathercode == 99) { textOut = "Гроза с градом"; codeOut = 200; }
  else { textOut = "??"; codeOut = 800; }
}

static void weathercodeToTomorrow(int weathercode1, String &textOut, int &codeOut) {
  if (weathercode1 == 0)  { textOut = "Ясно"; codeOut = 800; }
  else if (weathercode1 == 1)  { textOut = "Преимущественно ясно"; codeOut = 801; }
  else if (weathercode1 == 2)  { textOut = "Переменная облачность"; codeOut = 802; }
  else if (weathercode1 == 3)  { textOut = "Плотная облачность"; codeOut = 804; }
  else if (weathercode1 == 45) { textOut = "Туман"; codeOut = 701; }
  else if (weathercode1 == 48) { textOut = "Изморозь"; codeOut = 600; }
  else if (weathercode1 >= 51 && weathercode1 <= 57) { textOut = "Морось"; codeOut = 300; }
  else if (weathercode1 >= 61 && weathercode1 <= 67) { textOut = "Дождь"; codeOut = 300; }
  else if (weathercode1 >= 71 && weathercode1 <= 77) { textOut = "Снегопад"; codeOut = 601; }
  else if (weathercode1 >= 80 && weathercode1 <= 82) { textOut = "Ливень"; codeOut = 313; }
  else if (weathercode1 == 85 || weathercode1 == 86) { textOut = "Снег"; codeOut = 601; }
  else if (weathercode1 == 95) { textOut = "Гроза"; codeOut = 200; }
  else if (weathercode1 == 96 || weathercode1 == 99) { textOut = "Гроза с градом"; codeOut = 200; }
  else { textOut = "??"; codeOut = 800; }
}

// Переводит "id" условия погоды OpenWeatherMap (200-804) в ближайший
// эквивалент из схемы Open-Meteo (0-99).
static int owmIdToOpenMeteoCode(int id) {
  if (id >= 200 && id <= 232) return 95;
  if (id >= 300 && id <= 321) return 51;
  if (id == 500 || id == 501) return 61;
  if (id == 502 || id == 503 || id == 504) return 65;
  if (id == 511) return 66;
  if (id >= 520 && id <= 531) return 80;
  if (id == 600 || id == 601 || id == 602) return 73;
  if (id >= 611 && id <= 616) return 66;
  if (id >= 620 && id <= 622) return 85;
  if (id == 701 || id == 711 || id == 721 || id == 741) return 45;
  if (id == 731 || id == 751 || id == 761 || id == 762) return 45;
  if (id == 771 || id == 781) return 95;
  if (id == 800) return 0;
  if (id == 801) return 1;
  if (id == 802 || id == 803) return 2;
  if (id == 804) return 3;
  return 3;
}

static void localHourMinuteFromUnix(time_t utcUnix, long tzOffsetSec, int &hourOut, int &minuteOut) {
  time_t shifted = utcUnix + (time_t)tzOffsetSec;
  struct tm *t = gmtime(&shifted);
  if (!t) { hourOut = 0; minuteOut = 0; return; }
  hourOut = t->tm_hour;
  minuteOut = t->tm_min;
}

static String localDateStringFromUnix(time_t utcUnix, long tzOffsetSec) {
  time_t shifted = utcUnix + (time_t)tzOffsetSec;
  struct tm *t = gmtime(&shifted);
  char buf[11];
  if (t) snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
  else buf[0] = '\0';
  return String(buf);
}

// Сборка готовых строк для бегущей строки - одинаково для обоих провайдеров.
// Знак градуса Цельсия в бегущей строке - как в оригинале: одиночный байт \260 (0xB0, градус в cp1251)
// и буква C. Не UTF-8: utf8rus() такой байт пропускает как есть, и шрифт Heebo7pt8b рисует свой значок градуса.
static const char *WEATHER_DEG = "\260" "C";

static void buildScrollStrings() {
  weatherString = "         ";
  weatherString += "Сейчас в " + gorod + ": " + String(temperaturedarc, 1) + WEATHER_DEG + ", ";
  weatherString += "влажность " + String(humiditydarc) + "%, ";
  weatherString += "давление " + String(pressuredarc / 1.33372, 0) + " мм рт.ст., ";
  weatherString += "ветер " + degString + " " + String(windSpeeddarc, 1) + " м/с, ";
  weatherString += "облачность " + String(cloudCoverdarc) + "%, " + textcode + "                ";

  weatherStringZ = "Прогноз на завтра: температура от " + String((int)round(temperatureLowdarc)) + WEATHER_DEG +
                   " до " + String((int)round(temperatureHighdarc)) + WEATHER_DEG + ", ";
  weatherStringZ += "влажность " + String(humiditydarc0) + "%, ";
  weatherStringZ += "ветер " + degString1 + " " + String(windSpeeddarc0, 1) + " м/с, " + textcode1 + "                   ";
}

// ============================================================
// ДАННЫЕ ЗАПРОСА И СЫРОЙ ОТВЕТ
// ============================================================

// Снимок настроек на момент запроса - задача читает только его, а не
// глобальные переменные (их в это время может менять веб-интерфейс).
struct WeatherParams {
  float lat;
  float lon;
  int source;
  String owmKey;
  String pirateKey;
};

// Иконка Pirate Weather (Dark Sky) -> WMO-код (как у Open-Meteo)
static int pirateIconCode(const String &icon) {
  if (icon.startsWith("clear")) return 0;
  if (icon.indexOf("partly") >= 0) return 2;
  if (icon.indexOf("cloudy") >= 0 || icon == "wind") return 3;
  if (icon.indexOf("fog") >= 0) return 45;
  if (icon.indexOf("rain") >= 0) return 61;
  if (icon.indexOf("sleet") >= 0) return 66;
  if (icon.indexOf("snow") >= 0) return 71;
  if (icon.indexOf("hail") >= 0 || icon.indexOf("thunder") >= 0) return 95;
  return 3;
}

struct WeatherRaw {
  bool ok = false;
  int provider = 0;  // 1 = Open-Meteo, 2 = OpenWeatherMap
  String a;          // Open-Meteo: весь ответ; OWM: текущая погода
  String b;          // OWM: прогноз (может быть пустым, если не скачался)
};

// ============================================================
// СКАЧИВАНИЕ (выполняется в фоновой задаче или синхронно из setup)
// ============================================================

static bool downloadOpenMeteo(const WeatherParams &p, WeatherRaw &raw) {
  String params = "?latitude=" + String(p.lat, 6) +
               "&longitude=" + String(p.lon, 6) +
               "&hourly=temperature_2m,relativehumidity_2m,weathercode,surface_pressure,cloudcover,windspeed_10m,winddirection_10m&daily=weathercode,temperature_2m_max,temperature_2m_min,sunrise,sunset&current_weather=true&windspeed_unit=ms&timezone=auto";

  // Клиент объявляем ДО HTTPClient: при выходе HTTPClient уничтожается первым
  // и корректно закрывает соединение (иначе lwIP падает в pbuf_free).
#if defined(PIXELHUB_REMOTE)
  WiFiClientSecure client;
  client.setInsecure();
  String url = "https://YOUR-TUNNEL.example.com/om/v1/forecast" + params;
#else
  WiFiClient client;
  String url = "http://YOUR-SERVER:8082/om/v1/forecast" + params;
#endif

  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  if (!http.begin(client, url)) {
    Serial.println("Open-Meteo: не удалось открыть URL");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("Open-Meteo: ошибка HTTP %d (%s)\n", code, http.errorToString(code).c_str());
    http.end();
    client.stop();
    return false;
  }
  raw.a = http.getString();
  http.end();
  client.stop();
  raw.provider = 1;
  return true;
}

static bool downloadOWM(const WeatherParams &p, WeatherRaw &raw) {
  if (p.owmKey.length() == 0) {
    Serial.println("OpenWeatherMap: ключ не задан (см. настройки погоды в веб-интерфейсе)");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false); // каждый запрос - новое соединение (иначе второй запрос ловит тайм-аут -11)

  String urlCur = "https://api.openweathermap.org/data/2.5/weather?lat=" + String(p.lat, 6) +
                  "&lon=" + String(p.lon, 6) + "&appid=" + p.owmKey + "&units=metric&lang=ru";
  http.begin(client, urlCur);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("OpenWeatherMap: ошибка HTTP %d (текущая погода)\n", code);
    http.end();
    return false;
  }
  raw.a = http.getString();
  http.end();
  client.stop(); // гарантированно закрываем соединение перед вторым запросом

  // cnt=40 - 40 записей шагом 3 часа = 5 суток: нужно экрану прогноза (дни +2, +3, +4).
  // Разбирается с фильтром ArduinoJson (см. applyOWM), поэтому памяти уходит немного.
  String urlFc = "https://api.openweathermap.org/data/2.5/forecast?lat=" + String(p.lat, 6) +
                 "&lon=" + String(p.lon, 6) + "&cnt=40&appid=" + p.owmKey + "&units=metric&lang=ru";
  http.begin(client, urlFc);
  code = http.GET();
  if (code == HTTP_CODE_OK) {
    raw.b = http.getString();
  } else {
    // Текущая погода уже есть - прогноз просто останется прежним.
    Serial.printf("OpenWeatherMap: ошибка HTTP %d (прогноз) - будет только текущая погода\n", code);
  }
  http.end();
  raw.provider = 2;
  return true;
}


static bool downloadPirate(const WeatherParams &p, WeatherRaw &raw) {
  if (p.pirateKey.length() == 0) {
    Serial.println("Pirat Weather: ключ не задан (настройки погоды)");
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(9000);
  http.setReuse(false);
  // hourly не нужен: прогноз по дням есть в daily, а «завтра в это же время»
  // берём из текущих значений — так ответ в разы меньше и стабильнее по памяти.
  String url = "https://api.pirateweather.net/forecast/" + p.pirateKey + "/" +
               String(p.lat, 6) + "," + String(p.lon, 6) +
               "?units=si&lang=ru&exclude=minutely,hourly,alerts,flags";
  http.begin(client, url);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("Pirat Weather: ошибка HTTP %d\n", code);
    http.end();
    client.stop();
    return false;
  }
  raw.a = http.getString();
  http.end();
  client.stop();
  raw.provider = 3;
  return true;
}

// Выбор провайдера по настройке: 0=Авто, 1=только Open-Meteo, 2=только OWM
static bool downloadWeather(const WeatherParams &p, WeatherRaw &raw) {
  raw.a = "";
  raw.b = "";
  raw.provider = 0;
  raw.ok = false;

  // Домашняя сборка: три источника (Pirat Weather, OpenWeatherMap, сервер Open-Meteo).
  // Выездная: только два сервиса по ключам, без сервера.
  // 0 = Авто, 1 = Pirat Weather, 2 = OpenWeatherMap, 3 = сервер (только дома).
  if (p.source == 1) {
    raw.ok = downloadPirate(p, raw);
  } else if (p.source == 2) {
    raw.ok = downloadOWM(p, raw);
  } else if (p.source == 3) {
#if !defined(PIXELHUB_REMOTE)
    raw.ok = downloadOpenMeteo(p, raw);
#endif
  } else {
#if defined(PIXELHUB_REMOTE)
    raw.ok = downloadPirate(p, raw);
    if (!raw.ok) {
      Serial.println("⚠️ Pirat Weather недоступен, пробую OpenWeatherMap...");
      raw.ok = downloadOWM(p, raw);
    }
#else
    raw.ok = downloadPirate(p, raw);
    if (!raw.ok) {
      Serial.println("⚠️ Pirat Weather недоступен, пробую OpenWeatherMap...");
      raw.ok = downloadOWM(p, raw);
    }
    if (!raw.ok) {
      Serial.println("⚠️ OpenWeatherMap недоступен, пробую сервер Open-Meteo...");
      raw.ok = downloadOpenMeteo(p, raw);
    }
#endif
  }
  return raw.ok;
}

// ============================================================
// РАЗБОР (всегда в основном потоке) - только тут меняются глобальные данные
// ============================================================

static bool applyOpenMeteo(const String &line) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, line);
  if (error) {
    Serial.print("Open-Meteo: ошибка JSON: ");
    Serial.println(error.c_str());
    return false;
  }

  // При timezone=auto массив hourly начинается с 00:00 сегодняшних суток
  // по местному времени, поэтому "сейчас" = индекс hours, а не 0.
  int hourIndex = hours;
  if (hourIndex < 0 || hourIndex > 23) hourIndex = 0;

  temperaturedarc = doc["current_weather"]["temperature"].as<float>();
  windSpeeddarc = doc["current_weather"]["windspeed"].as<float>();
  int winddirection = doc["current_weather"]["winddirection"].as<int>();
  int weathercode = doc["current_weather"]["weathercode"].as<int>();
  humiditydarc = doc["hourly"]["relativehumidity_2m"][hourIndex].as<int>();
  pressuredarc = doc["hourly"]["surface_pressure"][hourIndex].as<float>();
  cloudCoverdarc = doc["hourly"]["cloudcover"][hourIndex].as<int>();

  temperatureLowdarc = doc["daily"]["temperature_2m_min"][1].as<float>();
  temperatureHighdarc = doc["daily"]["temperature_2m_max"][1].as<float>();
  int weathercode1 = doc["daily"]["weathercode"][1].as<int>();
  int winddirection_10m = doc["hourly"]["winddirection_10m"][24 + hourIndex].as<int>();
  humiditydarc0 = doc["hourly"]["relativehumidity_2m"][24 + hourIndex].as<int>();
  windSpeeddarc0 = doc["hourly"]["windspeed_10m"][24 + hourIndex].as<float>();

  degString  = windDirectionFromDegrees(winddirection);
  degString1 = windDirectionFromDegrees(winddirection_10m);

  weathercodeToNow(weathercode, textcode, location_code);
  weathercodeToTomorrow(weathercode1, textcode1, coded1);

  String sunriseStr = doc["daily"]["sunrise"][0].as<String>();
  String sunsetStr = doc["daily"]["sunset"][0].as<String>();
  parseTimeFromISO(sunriseStr, sunriseHour, sunriseMinute);
  parseTimeFromISO(sunsetStr, sunsetHour, sunsetMinute);

  // Open-Meteo по умолчанию отдаёт 7 дней; если пришло меньше, недостающие повторяют последний
  // полученный день (чтобы вместо нулей не показывать мусор)
  int dailyCount = doc["daily"]["weathercode"].size();
  for (int i = 0; i < FORECAST_DATA_DAYS; i++) {
    if (i < dailyCount) {
      forecastWeathercode[i] = doc["daily"]["weathercode"][i].as<int>();
      forecastTempMin[i] = doc["daily"]["temperature_2m_min"][i].as<float>();
      forecastTempMax[i] = doc["daily"]["temperature_2m_max"][i].as<float>();
    } else if (i > 0) {
      forecastWeathercode[i] = forecastWeathercode[i - 1];
      forecastTempMin[i] = forecastTempMin[i - 1];
      forecastTempMax[i] = forecastTempMax[i - 1];
    }
  }

  buildScrollStrings();
  weatherSourceUsed = "Open-Meteo (сервер)";
  Serial.println("✅ Погода обновлена (Open-Meteo сервер)");
  return true;
}


static bool applyPirate(const String &payload) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("Pirat Weather: ошибка JSON: ");
    Serial.println(err.c_str());
    return false;
  }

  JsonObject cur = doc["currently"];
  temperaturedarc = cur["temperature"] | 0.0f;
  float hum = cur["humidity"] | 0.0f;
  humiditydarc = (int)(hum * 100.0f + 0.5f);
  pressuredarc = cur["pressure"] | 1013.0f;
  windSpeeddarc = cur["windSpeed"] | 0.0f;      // units=si -> м/с
  float clouds = cur["cloudCover"] | 0.0f;
  cloudCoverdarc = (int)(clouds * 100.0f + 0.5f);
  int winddirection = cur["windBearing"] | 0;
  degString = windDirectionFromDegrees(winddirection);
  weathercodeToNow(pirateIconCode(cur["icon"] | "cloudy"), textcode, location_code);

  long tzOff = doc["offset"] | (long)((long)timezoneOffset * 3600L);
  JsonArray daily = doc["daily"]["data"];
  int n = daily.size();

  if (n > 0) {
    localHourMinuteFromUnix(daily[0]["sunriseTime"] | 0, tzOff, sunriseHour, sunriseMinute);
    localHourMinuteFromUnix(daily[0]["sunsetTime"] | 0, tzOff, sunsetHour, sunsetMinute);
  }
  if (n > 1) {
    temperatureHighdarc = daily[1]["temperatureHigh"] | temperaturedarc;
    temperatureLowdarc = daily[1]["temperatureLow"] | temperaturedarc;
    weathercodeToTomorrow(pirateIconCode(daily[1]["icon"] | "cloudy"), textcode1, coded1);
  } else {
    temperatureHighdarc = temperaturedarc;
    temperatureLowdarc = temperaturedarc;
    weathercodeToTomorrow(pirateIconCode(cur["icon"] | "cloudy"), textcode1, coded1);
  }
  humiditydarc0 = humiditydarc;
  windSpeeddarc0 = windSpeeddarc;
  degString1 = degString;

  for (int i = 0; i < FORECAST_DATA_DAYS; i++) {
    if (i < n) {
      forecastTempMax[i] = daily[i]["temperatureHigh"] | temperaturedarc;
      forecastTempMin[i] = daily[i]["temperatureLow"] | temperaturedarc;
      forecastWeathercode[i] = pirateIconCode(daily[i]["icon"] | "cloudy");
    } else if (i > 0) {
      forecastTempMax[i] = forecastTempMax[i - 1];
      forecastTempMin[i] = forecastTempMin[i - 1];
      forecastWeathercode[i] = forecastWeathercode[i - 1];
    }
  }

  buildScrollStrings();
  weatherSourceUsed = "Pirat Weather";
  Serial.println("✅ Погода обновлена (Pirat Weather)");
  return true;
}

static bool applyOWM(const String &curPayload, const String &fcPayload) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, curPayload);
  if (err) {
    Serial.print("OpenWeatherMap: ошибка JSON: ");
    Serial.println(err.c_str());
    return false;
  }

  temperaturedarc = doc["main"]["temp"] | 0.0f;
  humiditydarc = doc["main"]["humidity"] | 0;
  pressuredarc = doc["main"]["pressure"] | 1013.0f; // гПа, как у Open-Meteo
  windSpeeddarc = doc["wind"]["speed"] | 0.0f;      // units=metric -> м/с
  int winddirection = doc["wind"]["deg"] | 0;
  int owmId = doc["weather"][0]["id"] | 800;
  cloudCoverdarc = doc["clouds"]["all"] | 0;
  degString = windDirectionFromDegrees(winddirection);
  weathercodeToNow(owmIdToOpenMeteoCode(owmId), textcode, location_code);

  long tzOffsetSec = doc["timezone"] | (long)((long)timezoneOffset * 3600L);
  time_t sunriseUnix = doc["sys"]["sunrise"] | 0;
  time_t sunsetUnix = doc["sys"]["sunset"] | 0;
  localHourMinuteFromUnix(sunriseUnix, tzOffsetSec, sunriseHour, sunriseMinute);
  localHourMinuteFromUnix(sunsetUnix, tzOffsetSec, sunsetHour, sunsetMinute);

  if (fcPayload.length() == 0) {
    buildScrollStrings();
    weatherSourceUsed = "OpenWeatherMap";
    Serial.println("✅ Погода обновлена (OpenWeatherMap, без прогноза)");
    return true;
  }

  // Фильтр: из 40 записей прогноза оставляем только нужные поля - так разбор занимает
  // в несколько раз меньше памяти, чем целиком (стабильность важнее).
  JsonDocument filter;
  filter["city"]["timezone"] = true;
  filter["list"][0]["dt"] = true;
  filter["list"][0]["main"]["temp"] = true;
  filter["list"][0]["main"]["temp_min"] = true;
  filter["list"][0]["main"]["temp_max"] = true;
  filter["list"][0]["main"]["humidity"] = true;
  filter["list"][0]["wind"]["speed"] = true;
  filter["list"][0]["wind"]["deg"] = true;
  filter["list"][0]["weather"][0]["id"] = true;

  JsonDocument docF;
  err = deserializeJson(docF, fcPayload, DeserializationOption::Filter(filter));
  if (!err) {
    long tzOff = docF["city"]["timezone"] | tzOffsetSec;
    JsonArray list = docF["list"];

    // Данные по дням, индекс = сколько суток от сегодняшней даты (0=сегодня ... 4)
    float dayMin[FORECAST_DATA_DAYS], dayMax[FORECAST_DATA_DAYS];
    int dayIdSample[FORECAST_DATA_DAYS];
    int dayBestDist[FORECAST_DATA_DAYS];   // насколько выбранная запись далека от полудня (ищем ближайшую к 12:00)
    bool dayHas[FORECAST_DATA_DAYS];
    bool dayNoon[FORECAST_DATA_DAYS];      // есть ли запись около полудня (11-15 ч): без неё день неполный
    for (int i = 0; i < FORECAST_DATA_DAYS; i++) {
      dayMin[i] = 999; dayMax[i] = -999; dayIdSample[i] = owmId; dayBestDist[i] = 99; dayHas[i] = false; dayNoon[i] = false;
    }

    // "Сегодня" берём по часам устройства (чтобы вечером, когда первая запись уже завтрашняя,
    // дни не сдвигались). Если время ещё не синхронизировано - как раньше, по первой записи.
    time_t nowUnix = time(nullptr);
    long todayNum = (nowUnix > 8 * 3600) ? (long)((nowUnix + tzOff) / 86400) : -1;

    bool sameTimeFound = false;

    for (JsonObject item : list) {
      time_t dtUnix = item["dt"] | 0;
      if (dtUnix == 0) continue;
      long dnum = (long)((dtUnix + tzOff) / 86400);
      if (todayNum < 0) todayNum = dnum;
      int dayIdx = (int)(dnum - todayNum);
      if (dayIdx < 0 || dayIdx >= FORECAST_DATA_DAYS) continue;

      int hh, mm;
      localHourMinuteFromUnix(dtUnix, tzOff, hh, mm);

      float tmin = item["main"]["temp_min"] | (float)(item["main"]["temp"] | 0.0f);
      float tmax = item["main"]["temp_max"] | (float)(item["main"]["temp"] | 0.0f);
      if (tmin < dayMin[dayIdx]) dayMin[dayIdx] = tmin;
      if (tmax > dayMax[dayIdx]) dayMax[dayIdx] = tmax;
      dayHas[dayIdx] = true;
      if (hh >= 11 && hh <= 15) dayNoon[dayIdx] = true;
      int dist = abs(hh - 12);
      if (dist < dayBestDist[dayIdx]) { // ближе к полудню - самый показательный код погоды на день
        dayBestDist[dayIdx] = dist;
        dayIdSample[dayIdx] = item["weather"][0]["id"] | 800;
      }

      // "Завтра в это же время" - ближайшая по времени запись НЕ сегодняшней даты
      if (!sameTimeFound && dayIdx >= 1 && abs(hh - hours) <= 1) {
        humiditydarc0 = item["main"]["humidity"] | 0;
        windSpeeddarc0 = item["wind"]["speed"] | 0.0f;
        int wd0 = item["wind"]["deg"] | 0;
        degString1 = windDirectionFromDegrees(wd0);
        int id1 = item["weather"][0]["id"] | 800;
        weathercodeToTomorrow(owmIdToOpenMeteoCode(id1), textcode1, coded1);
        sameTimeFound = true;
      }
    }

    // Дни, по которым данных нет (например, "сегодня" поздним вечером): берём ближайший день,
    // у которого они есть, - сначала предыдущий, иначе следующий, иначе текущую погоду.
    // Для дней +2..+4 нужна запись около полудня: последний (пятый) день у OWM бывает неполным -
    // только ночные записи, и максимум был бы сильно занижен.
    for (int i = 2; i < FORECAST_DATA_DAYS; i++) if (!dayNoon[i]) dayHas[i] = false;

    for (int i = 0; i < FORECAST_DATA_DAYS; i++) {
      if (dayHas[i]) {
        forecastTempMin[i] = dayMin[i];
        forecastTempMax[i] = dayMax[i];
        forecastWeathercode[i] = owmIdToOpenMeteoCode(dayIdSample[i]);
        continue;
      }
      int src = -1;
      for (int j = i - 1; j >= 0 && src < 0; j--) if (dayHas[j]) src = j;
      for (int j = i + 1; j < FORECAST_DATA_DAYS && src < 0; j++) if (dayHas[j]) src = j;
      if (src >= 0) {
        forecastTempMin[i] = dayMin[src];
        forecastTempMax[i] = dayMax[src];
        forecastWeathercode[i] = owmIdToOpenMeteoCode(dayIdSample[src]);
      } else {
        forecastTempMin[i] = temperaturedarc;
        forecastTempMax[i] = temperaturedarc;
        forecastWeathercode[i] = owmIdToOpenMeteoCode(owmId);
      }
    }
    if (dayHas[1]) {
      temperatureLowdarc = dayMin[1];
      temperatureHighdarc = dayMax[1];
    }
    if (!sameTimeFound) {
      humiditydarc0 = humiditydarc;
      windSpeeddarc0 = windSpeeddarc;
      degString1 = degString;
      weathercodeToTomorrow(forecastWeathercode[1], textcode1, coded1);
    }
  } else {
    Serial.print("OpenWeatherMap: ошибка JSON прогноза: ");
    Serial.println(err.c_str());
  }

  buildScrollStrings();
  weatherSourceUsed = "OpenWeatherMap";
  Serial.println("✅ Погода обновлена (OpenWeatherMap)");
  return true;
}

static bool applyWeather(const WeatherRaw &raw) {
  if (!raw.ok) return false;
  if (raw.provider == 1) return applyOpenMeteo(raw.a);
  if (raw.provider == 2) return applyOWM(raw.a, raw.b);
  if (raw.provider == 3) return applyPirate(raw.a);
  return false;
}

// ============================================================
// СИНХРОННЫЙ РЕЖИМ - только для setup()
// ============================================================
bool fetchWeather() {
  if (!weatherEnabled) return false;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi нет, погода не запрошена");
    return false;
  }
  WeatherParams p = { lat, lon, weatherSource, owmApiKey, pirateApiKey };
  WeatherRaw raw;
  if (!downloadWeather(p, raw)) return false;
  return applyWeather(raw);
}

// ============================================================
// АСИНХРОННЫЙ РЕЖИМ - фоновая задача для loop()
// ============================================================
static TaskHandle_t weatherTaskHandle = nullptr;
static std::atomic<bool> weatherBusy(false);
static std::atomic<bool> weatherResultReady(false);
static WeatherParams weatherTaskParams = { 0, 0, 0, "" };
static WeatherRaw weatherTaskRaw;

static void weatherTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // спим, пока не попросят обновиться
    downloadWeather(weatherTaskParams, weatherTaskRaw);
    weatherResultReady = true;
    weatherBusy = false;
  }
}

void startWeatherTask() {
  if (weatherTaskHandle != nullptr) return;
  // Стек 16 КБ - TLS-рукопожатие (mbedtls) требует заметно больше стандартных 8 КБ.
  // Ядро 0 (там же Wi-Fi), приоритет 1 - ниже основного цикла, анимацию не тормозит.
  xTaskCreatePinnedToCore(weatherTask, "weather", 16384, nullptr, 1, &weatherTaskHandle, 0);
}

bool requestWeatherUpdate() {
  if (weatherTaskHandle == nullptr) return false;
  if (weatherBusy) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  weatherTaskParams = { lat, lon, weatherSource, owmApiKey, pirateApiKey }; // снимок настроек
  weatherResultReady = false;
  weatherBusy = true;
  xTaskNotifyGive(weatherTaskHandle);
  return true;
}

bool pollWeatherResult(bool &success) {
  if (!weatherResultReady) return false;
  success = applyWeather(weatherTaskRaw);
  weatherTaskRaw.a = ""; // освобождаем память под ответ
  weatherTaskRaw.b = "";
  weatherResultReady = false;
  return true;
}
