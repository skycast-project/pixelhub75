#include "web_server.h"
#include "web_send.h"
#include "dfplayer.h"
#include "alarm_df.h"
#include "local_sensors.h"
#include "air_quality.h"
#include "rtc_clock.h"
#include <WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include "globals.h"
#include "fonts.h"
#include "utils.h"
#include "storage.h"
#include "screen.h"
#include "big_clock.h"
#include "display_manager.h"
#include "sensors.h"
#include "radio.h"
#include "stations.h"
#include <ArduinoJson.h>
#include "integrations.h"
#include "night_screen.h"
#include "pins_config.h"
#include "logo_data.h"

String loadTemplate(const String &path) {
  pageMemBegin();
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("❌ Файл не найден: " + path);
    pageMemEnd();
    return "<!DOCTYPE html><html><head><meta charset='utf-8'><title>PixelHub</title></head><body><h1>Файл не найден</h1><p>Загрузите HTML-файлы в LittleFS.</p><p>IP: " + WiFi.localIP().toString() + "</p></body></html>";
  }
  // Читаем с заранее выделенным буфером: readString() переаллоцирует
  // строку по мере роста, а на больших шаблонах это рвёт кучу.
  // Пробуем дважды: при разовом сбое выделения куча обычно успевает отдать блок.
  String content;
  for (int attempt = 0; attempt < 2; attempt++) {
    content = String();
    content.reserve(file.size() + 1);
    char chunk[256];
    while (file.available()) {
      size_t n = file.read((uint8_t *)chunk, sizeof(chunk));
      for (size_t i = 0; i < n; i++) content += chunk[i];
    }
    if (content.length() > 0) break;
    file.seek(0);
    delay(20);
    Serial.printf("⚠️ шаблон %s прочитан пустым, повтор\n", path.c_str());
  }
  file.close();
  // Небольшой запас под replace(): большинство подстановок той же длины или
  // короче. Большой reserve() (1.5x) на больших страницах сам по себе требует
  // непрерывный блок ~12 КБ и при фрагментации кучи срывается.
  content.reserve(content.length() + 256);
  pageMemEnd();   // резерв возвращаем после того, как страница собрана
  // Обычные кнопки «Сохранить» убраны: сохраняет только плавающая кнопка.
  content.replace("<button type=\"submit\">Сохранить</button>", "");
  content.replace("<button type=\"submit\">Сохранить и перезагрузить</button>", "");
  content.replace("<button type=\"submit\">Сохранить настройки</button>", "");

  // В интерфейсе источник погоды называется Pirat Weather
  content.replace("Open-Meteo", "Pirat Weather");
  // иконка вкладки браузера
  content.replace("<head>", "<head><link rel='icon' type='image/png' href='/favicon.ico'>");
  // логотип над меню на всех страницах
  content.replace("<div class=\"nav\">",
                  "<div style='text-align:center;margin:10px 0 6px'>"
                  "<img src='/logo.png' alt='PixelHub 75' "
                  "style='width:88px;height:88px;border-radius:18px;box-shadow:0 0 18px rgba(60,160,255,.35)'>"
                  "</div><div class=\"nav\">");
  return content;
}

void processTemplate(String &html) {
  // Шрифт часов:минут - 7 вариантов (как fch в оригинале)
  for (int i = 0; i < CLOCK_FONT_COUNT; i++) {
    html.replace("{{selClk" + String(i) + "}}", (clockFontIndex == i) ? "selected" : "");
  }
  // Шрифт секунд/даты/дня недели - 6 вариантов (как fss в оригинале)
  for (int i = 0; i < SEC_FONT_COUNT; i++) {
    html.replace("{{selSec" + String(i) + "}}", (secFontIndex == i) ? "selected" : "");
  }
  html.replace("{{clockColor}}", clockColorHex);
  html.replace("{{secColor}}", secondsColorHex);
  html.replace("{{checkedSec}}", showSeconds ? "checked" : "");

  html.replace("{{checkedHourly}}", hourlyAnnounceEnabled ? "checked" : "");
  for (int i = 0; i < 4; i++) {
    html.replace("{{selVoice" + String(i) + "}}", (hourlyVoiceMode == i) ? "selected" : "");
  }

  // 3 строки бегущей строки - плейсхолдеры {{scrollColor0}}/{{scrollSpeed0}}
  // и т.д. до индекса 2, каждая строка настраивается отдельно
  for (int i = 0; i < SCROLL_LINE_COUNT; i++) {
    html.replace("{{scrollColor" + String(i) + "}}", scrollLineColorHex[i]);
    html.replace("{{scrollSpeed" + String(i) + "}}", String(scrollLineSpeed[i]));
    html.replace("{{checkedLine" + String(i) + "}}", scrollLineEnabled[i] ? "checked" : "");
  }

  html.replace("{{dayColor}}", dayColorHex);
  html.replace("{{dateColor}}", dateColorHex);
  html.replace("{{monthColor}}", monthColorHex);
  html.replace("{{checkedWeekend}}", weekendColorEnabled ? "checked" : "");
  html.replace("{{weekendDayColor}}", weekendDayColorHex);
  html.replace("{{weekendDateColor}}", weekendDateColorHex);

}

void handleRoot(AsyncWebServerRequest *request) {
  String html = loadTemplate("/index.html");
  html.replace("{{ip}}", WiFi.localIP().toString());
  html.replace("<a href=\"/settings/wifi\">Wi-Fi</a>",
               "<a href=\"/settings/pins\">Пины матрицы</a><a href=\"/settings/wifi\">Wi-Fi</a>");
  html.replace("{{gorod}}", gorod);
  html.replace("{{currentTemp}}", String(temperaturedarc, 1));
  html.replace("{{weatherDesc}}", textcode);
  html.replace("{{brightnessMode}}", autoBrightnessEnabled ? "Авто (восход/закат)" : "Ручная");
  // Источник, из которого реально пришла погода (Pirat Weather / OpenWeatherMap / сервер)
  html.replace("Яркость: {{brightnessMode}}</p>",
               String("Яркость: {{brightnessMode}}<br>Источник погоды: ") + weatherSourceUsed + "</p>");

  // Индикатор датчика "дома:" - цветной кружок + подпись, что реально нашли
  String sensorColor, sensorText;
  switch (detectedSensor) {
    case SensorType::BME280:
      sensorColor = "#2ecc71"; // зелёный
      sensorText = "BME280 найден (темп, влажность, давление)";
      break;
    case SensorType::BMP280:
      sensorColor = "#f1c40f"; // жёлтый - работает, но без влажности
      sensorText = "BMP280 найден (темп, давление - без влажности)";
      break;
    default:
      sensorColor = "#e74c3c"; // красный
      sensorText = "Датчик не найден";
      break;
  }
  html.replace("{{sensorColor}}", sensorColor);
  // Полный список датчиков проекта: зелёный - подключён, красный - нет
  String rows;
  auto addRow = [&](const String &name, const String &val, bool ok) {
    const char *col = ok ? "#27ae60" : "#e74c3c";
    rows += "<div class='status-row'><div class='status-dot' style='background:";
    rows += col; rows += ";box-shadow:0 0 8px "; rows += col; rows += "'></div><span><b>";
    rows += name; rows += "</b> — "; rows += val; rows += "</span></div>";
  };
  addRow("BME280", detectedSensor == SensorType::BME280 ? "темп+влажн+давл" : "не найден", detectedSensor == SensorType::BME280);
  addRow("BMP280", detectedSensor == SensorType::BMP280 ? "темп+давление" : "не найден", detectedSensor == SensorType::BMP280);
  addRow("BMP180", detectedSensor == SensorType::BMP180 ? "темп+давление" : "не найден", detectedSensor == SensorType::BMP180);
  addRow("AHT10/20", detectedSensor == SensorType::AHT10 ? "темп+влажность" : "не найден", detectedSensor == SensorType::AHT10);
  addRow("SHT21", detectedSensor == SensorType::SHT21 ? "темп+влажность" : "не найден", detectedSensor == SensorType::SHT21);
  addRow("SGP30", airValid ? ("eCO2 " + String((int)airCo2) + " ppm, TVOC " + String((int)airTvoc) + " ppb") : "не найден", airValid);
  addRow("DS18B20", outdoorTempValid ? (String(outdoorTemp, 1) + " °C") : "не найден", outdoorTempValid);
  addRow("DS3231", rtcPresent() ? "часы реального времени" : "не найден", rtcPresent());
  addRow("DFPlayer", dfpReady() ? "плеер по UART" : "не найден", dfpReady());
  int shown = localSensorsShownCount();
  addRow("Wi-Fi датчики", shown ? (String(shown) + " шт.") : "нет", shown > 0);
  String inject = sensorText + "</span></div></div><div class='card'><div class='line-title'>Датчики проекта</div>" + rows +
                  "<p style='margin-top:12px'><a href='/settings/clock' style='margin-right:14px'>Часы, звук и будильник</a><a href='/settings/hwpins' style='margin-right:14px'>Пины датчиков</a>"
                  "<a href='/api/reboot' style='display:inline-block;padding:8px 14px;border-radius:8px;background:#c0392b;color:#fff;text-decoration:none'>Перезагрузить плату</a></p>"
                  "<div style='display:none'><div class='status-row'><div class='status-dot' style='display:none'></div><span>";
  html.replace("{{sensorText}}", inject);

  sendHtmlChunked(request, html);
}


void handleClockSettings(AsyncWebServerRequest *request) {
  String html = loadTemplate("/clock.html");
  for (int i = 0; i < CLOCK_FONT_COUNT; i++)
    html.replace("{{selClk" + String(i) + "}}", (clockFontIndex == i) ? "selected" : "");
  for (int i = 0; i < SEC_FONT_COUNT; i++)
    html.replace("{{selSec" + String(i) + "}}", (secFontIndex == i) ? "selected" : "");
  html.replace("{{clockColor}}", clockColorHex);
  html.replace("{{secColor}}", secondsColorHex);
  html.replace("{{checkedSec}}", showSeconds ? "checked" : "");
  html.replace("{{dayColor}}", dayColorHex);
  html.replace("{{dateColor}}", dateColorHex);
  html.replace("{{monthColor}}", monthColorHex);
  html.replace("{{checkedWeekend}}", weekendColorEnabled ? "checked" : "");
  html.replace("{{weekendDayColor}}", weekendDayColorHex);
  html.replace("{{weekendDateColor}}", weekendDateColorHex);
  html += "<div class=\"card\"><div class=\"line-title\">Звук и будильник</div>"
          "<p class=\"hint\" style=\"margin:0\"><a href=\"/settings/sound\">Озвучка (DFPlayer)</a> &nbsp;|&nbsp; "
          "<a href=\"/settings/alarm\">Будильник</a></p></div>";
  sendHtmlChunked(request, html);
}

void handleClockSettingsPost(AsyncWebServerRequest *request) {
  if (request->hasParam("font", true)) clockFontIndex = request->getParam("font", true)->value().toInt();
  if (request->hasParam("secFont", true)) secFontIndex = request->getParam("secFont", true)->value().toInt();
  if (request->hasParam("clockColor", true)) {
    clockColorHex = request->getParam("clockColor", true)->value();
    clockColor = hexToColor(clockColorHex);
  }
  if (request->hasParam("secColor", true)) {
    secondsColorHex = request->getParam("secColor", true)->value();
    secondsColor = hexToColor(secondsColorHex);
  }
  if (request->hasParam("dayColor", true)) {
    dayColorHex = request->getParam("dayColor", true)->value();
    dayColor = hexToColor(dayColorHex);
  }
  if (request->hasParam("dateColor", true)) {
    dateColorHex = request->getParam("dateColor", true)->value();
    dateColor = hexToColor(dateColorHex);
  }
  if (request->hasParam("monthColor", true)) {
    monthColorHex = request->getParam("monthColor", true)->value();
    monthColor = hexToColor(monthColorHex);
  }
  weekendColorEnabled = request->hasParam("weekendEnabled", true);
  if (request->hasParam("weekendDayColor", true)) {
    weekendDayColorHex = request->getParam("weekendDayColor", true)->value();
    weekendDayColor = hexToColor(weekendDayColorHex);
  }
  if (request->hasParam("weekendDateColor", true)) {
    weekendDateColorHex = request->getParam("weekendDateColor", true)->value();
    weekendDateColor = hexToColor(weekendDateColorHex);
  }
  showSeconds = request->hasParam("showSec", true);
  hourlyAnnounceEnabled = request->hasParam("hourlyEnabled", true);
  if (request->hasParam("hourlyVoice", true)) hourlyVoiceMode = request->getParam("hourlyVoice", true)->value().toInt();
  if (request->hasParam("hourlyVolume", true)) {
    int v = request->getParam("hourlyVolume", true)->value().toInt();
    if (v >= 0 && v <= 100) hourlyVolumePercent = v;
  }
  saveConfig();
  request->redirect("/settings/clock");
}

void handleScrollSettings(AsyncWebServerRequest *request) {
  std::vector<std::pair<String, String>> r;
  auto A = [&](const char *k, const String &v) { r.push_back({String(k), v}); };

  for (int i = 0; i < CLOCK_FONT_COUNT; i++)
    A(("{{selClk" + String(i) + "}}").c_str(), (clockFontIndex == i) ? "selected" : "");
  for (int i = 0; i < SEC_FONT_COUNT; i++)
    A(("{{selSec" + String(i) + "}}").c_str(), (secFontIndex == i) ? "selected" : "");
  A("{{clockColor}}", clockColorHex);
  A("{{secColor}}", secondsColorHex);
  A("{{checkedSec}}", showSeconds ? "checked" : "");
  A("{{checkedHourly}}", hourlyAnnounceEnabled ? "checked" : "");
  for (int i = 0; i < 4; i++)
    A(("{{selVoice" + String(i) + "}}").c_str(), (hourlyVoiceMode == i) ? "selected" : "");
  for (int i = 0; i < SCROLL_LINE_COUNT; i++) {
    A(("{{scrollColor" + String(i) + "}}").c_str(), scrollLineColorHex[i]);
    A(("{{scrollSpeed" + String(i) + "}}").c_str(), String(scrollLineSpeed[i]));
    A(("{{checkedLine" + String(i) + "}}").c_str(), scrollLineEnabled[i] ? "checked" : "");
  }
  A("{{dayColor}}", dayColorHex);
  A("{{dateColor}}", dateColorHex);
  A("{{monthColor}}", monthColorHex);
  A("{{checkedWeekend}}", weekendColorEnabled ? "checked" : "");
  A("{{weekendDayColor}}", weekendDayColorHex);
  A("{{weekendDateColor}}", weekendDateColorHex);

  sendTemplateChunked(request, "/scroll.html", std::move(r));
}

void handleScrollSettingsPost(AsyncWebServerRequest *request) {
  // Ожидаемые имена полей формы: speed0/speed1/speed2, scrollColor0/1/2,
  // enabled0/1/2/3 (чекбокс "показывать строку")
  for (int i = 0; i < SCROLL_LINE_COUNT; i++) {
    String speedParam = "speed" + String(i);
    String colorParam = "scrollColor" + String(i);
    String enabledParam = "enabled" + String(i);
    if (request->hasParam(speedParam, true)) {
      int spd = request->getParam(speedParam, true)->value().toInt();
      if (spd >= 1 && spd <= 10) scrollLineSpeed[i] = spd;
    }
    if (request->hasParam(colorParam, true)) {
      scrollLineColorHex[i] = request->getParam(colorParam, true)->value();
      scrollLineColor[i] = hexToColor(scrollLineColorHex[i]);
    }
    scrollLineEnabled[i] = request->hasParam(enabledParam, true);
  }
  scrollOffset = startOffset;
  currentScrollLine = 0;
  updateFullScrollText();
  saveConfig();
  request->redirect("/settings/scroll");
}

void handleWifiSettings(AsyncWebServerRequest *request) {
  String html = loadTemplate("/wifi.html");
  html.replace("{{ssid}}", wifi_ssid);
  html.replace("{{password}}", wifi_password);
  html.replace("{{timezone}}", String(timezoneOffset));
  sendHtmlChunked(request, html);
}

void handleWifiSettingsPost(AsyncWebServerRequest *request) {
  if (request->hasParam("ssid", true)) wifi_ssid = request->getParam("ssid", true)->value();
  if (request->hasParam("password", true)) wifi_password = request->getParam("password", true)->value();
  if (request->hasParam("timezone", true)) timezoneOffset = request->getParam("timezone", true)->value().toInt();
  saveConfig();
  String html =
    "<!DOCTYPE html><html lang='ru'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>PixelHub 75</title><link rel='stylesheet' href='/style.css'></head><body>"
    "<div class='wrap'><div class='card center-card'>"
    "<div class='big-ico'>📶</div>"
    "<h2>Настройки сохранены</h2>"
    "<p class='hint'>Устройство перезагружается и подключается к новой сети...</p>"
    "</div></div>"
    "<script>setTimeout(function(){location='/';}, 4000);</script>"
    "</body></html>";
  sendHtmlChunked(request, html);
  delay(1000);
  ESP.restart();
}

void handleBrightnessSettings(AsyncWebServerRequest *request) {
  String html = loadTemplate("/brightness.html");
  html.replace("{{brightness}}", String(displayBrightness));
  html.replace("{{nightBrightness}}", String(nightBrightness));
  html.replace("{{checkedAutoBrightness}}", autoBrightnessEnabled ? "checked" : "");
  sendHtmlChunked(request, html);
}

void handleBrightnessSettingsPost(AsyncWebServerRequest *request) {
  if (request->hasParam("brightness", true)) {
    int val = request->getParam("brightness", true)->value().toInt();
    if (val >= 0 && val <= 255) displayBrightness = val;
  }
  if (request->hasParam("nightBrightness", true)) {
    int val = request->getParam("nightBrightness", true)->value().toInt();
    if (val >= 0 && val <= 255) nightBrightness = val;
  }
  autoBrightnessEnabled = request->hasParam("autoBrightness", true);

  if (autoBrightnessEnabled) {
    applyAutoBrightness();
  } else {
    display->setBrightness8(displayBrightness);
  }
  saveConfig();
  request->redirect("/settings/brightness");
}

void handleWeatherSettings(AsyncWebServerRequest *request) {
  std::vector<std::pair<String, String>> r;
  auto A = [&](const char *k, const String &v) { r.push_back({String(k), v}); };

  A("{{lat}}", String(lat, 6));
  A("{{lon}}", String(lon, 6));
  A("{{gorod}}", gorod);
  A("{{checkedEnabled}}", weatherEnabled ? "checked" : "");
  A("{{interval}}", String(weatherUpdateInterval));
  A("{{checkedForecastScreen}}", forecastScreenEnabled ? "checked" : "");
  for (int i = 0; i < SEC_FONT_COUNT; i++)
    A(("{{selFcDay" + String(i) + "}}").c_str(), (forecastDayFontIndex == i) ? "selected" : "");
  A("{{checkedWeatherNowScreen}}", weatherNowScreenEnabled ? "checked" : "");
  A("{{checkedHomeScreen}}", homeScreenEnabled ? "checked" : "");

  A("{{selSource0}}", (weatherSource == 0) ? "selected" : "");
  A("{{selSource1}}", (weatherSource == 1) ? "selected" : "");
  A("{{selSource2}}", (weatherSource == 2) ? "selected" : "");
  A("{{owmApiKey}}", owmApiKey);
  A("{{pirateApiKey}}", pirateApiKey);

  A("Авто (Open-Meteo, при сбое — OpenWeatherMap)",
    "Авто (Pirat Weather, при сбое — OpenWeatherMap)");
  A("Только Open-Meteo", "Только Pirat Weather");
#if !defined(PIXELHUB_REMOTE)
  A("Только OpenWeatherMap</option>",
    "Только OpenWeatherMap</option><option value=\"3\" {{selSource3}}>Только сервер (Open-Meteo)</option>");
  A("{{selSource3}}", (weatherSource == 3) ? "selected" : "");
#endif

  A("{{forecastDayColor}}", forecastDayColorHex);
  A("{{forecastTempMinColor}}", forecastTempMinColorHex);
  A("{{forecastTempMaxColor}}", forecastTempMaxColorHex);

  A("{{weatherTitleColor}}", weatherTitleColorHex);
  A("{{weatherDateColor}}", weatherDateColorHex);
  A("{{weatherTimeColor}}", weatherTimeColorHex);
  A("{{weatherTempNowColor}}", weatherTempNowColorHex);
  A("{{weatherTempTomorrowColor}}", weatherTempTomorrowColorHex);

  A("{{homeTitleColor}}", homeTitleColorHex);
  A("{{homeTimeColor}}", homeTimeColorHex);
  A("{{homePressureColor}}", homePressureColorHex);
  A("{{homeTempColor}}", homeTempColorHex);
  A("{{homeHumidityColor}}", homeHumidityColorHex);
  A("{{homeTempOffset}}", String(homeTempOffset, 1));
  A("{{homePressureOffset}}", String(homePressureOffsetMm, 0));
  A("{{homeHumidityOffset}}", String(homeHumidityOffset, 0));

  String airCard = "<div class=\"wrap\"><div class=\"card\"><div class=\"line-title\">Цвета экрана «Качество воздуха»</div>"
    "<form method=\"POST\" action=\"/settings/air\">"
    "<label for=\"airTitle\">Заголовок «Воздух»</label><input type=\"color\" id=\"airTitle\" name=\"airTitle\" value=\"" + airTitleColorHex + "\">"
    "<label for=\"airCo2\">eCO2</label><input type=\"color\" id=\"airCo2\" name=\"airCo2\" value=\"" + airCo2ColorHex + "\">"
    "<label for=\"airTvoc\">TVOC</label><input type=\"color\" id=\"airTvoc\" name=\"airTvoc\" value=\"" + airTvocColorHex + "\">"
    "<label for=\"airBar\">Полоса TVOC</label><input type=\"color\" id=\"airBar\" name=\"airBar\" value=\"" + airBarColorHex + "\">"
    "<button type=\"submit\">Сохранить</button></form></div></div></body>";
  A("</body>", airCard);

  sendTemplateChunked(request, "/weather.html", std::move(r));
}

void handleWeatherSettingsPost(AsyncWebServerRequest *request) {
  if (request->hasParam("lat", true)) lat = request->getParam("lat", true)->value().toFloat();
  if (request->hasParam("lon", true)) lon = request->getParam("lon", true)->value().toFloat();
  if (request->hasParam("gorod", true)) gorod = request->getParam("gorod", true)->value();
  weatherEnabled = request->hasParam("enabled", true);
  forecastScreenEnabled = request->hasParam("forecastScreen", true);
  if (request->hasParam("forecastDayFont", true)) {
    int v = request->getParam("forecastDayFont", true)->value().toInt();
    if (v >= 0 && v < SEC_FONT_COUNT) forecastDayFontIndex = v;
  }
  weatherNowScreenEnabled = request->hasParam("weatherNowScreen", true);
  homeScreenEnabled = request->hasParam("homeScreen", true);
  if (request->hasParam("interval", true)) {
    int val = request->getParam("interval", true)->value().toInt();
    if (val >= 60) weatherUpdateInterval = val;
  }

  if (request->hasParam("weatherSource", true)) {
    int val = request->getParam("weatherSource", true)->value().toInt();
#if defined(PIXELHUB_REMOTE)
    if (val >= 0 && val <= 2) weatherSource = val;   // выездная: сервера нет
#else
    if (val >= 0 && val <= 3) weatherSource = val;   // домашняя: есть и сервер
#endif
  }
  if (request->hasParam("pirateApiKey", true)) {
    pirateApiKey = request->getParam("pirateApiKey", true)->value();
    pirateApiKey.trim();
  }
  if (request->hasParam("owmApiKey", true)) {
    owmApiKey = request->getParam("owmApiKey", true)->value();
  }

  if (request->hasParam("forecastDayColor", true)) {
    forecastDayColorHex = request->getParam("forecastDayColor", true)->value();
    forecastDayColor = hexToColor(forecastDayColorHex);
  }
  if (request->hasParam("forecastTempMinColor", true)) {
    forecastTempMinColorHex = request->getParam("forecastTempMinColor", true)->value();
    forecastTempMinColor = hexToColor(forecastTempMinColorHex);
  }
  if (request->hasParam("forecastTempMaxColor", true)) {
    forecastTempMaxColorHex = request->getParam("forecastTempMaxColor", true)->value();
    forecastTempMaxColor = hexToColor(forecastTempMaxColorHex);
  }

  if (request->hasParam("weatherTitleColor", true)) {
    weatherTitleColorHex = request->getParam("weatherTitleColor", true)->value();
    weatherTitleColor = hexToColor(weatherTitleColorHex);
  }
  if (request->hasParam("weatherDateColor", true)) {
    weatherDateColorHex = request->getParam("weatherDateColor", true)->value();
    weatherDateColor = hexToColor(weatherDateColorHex);
  }
  if (request->hasParam("weatherTimeColor", true)) {
    weatherTimeColorHex = request->getParam("weatherTimeColor", true)->value();
    weatherTimeColor = hexToColor(weatherTimeColorHex);
  }
  if (request->hasParam("weatherTempNowColor", true)) {
    weatherTempNowColorHex = request->getParam("weatherTempNowColor", true)->value();
    weatherTempNowColor = hexToColor(weatherTempNowColorHex);
  }
  if (request->hasParam("weatherTempTomorrowColor", true)) {
    weatherTempTomorrowColorHex = request->getParam("weatherTempTomorrowColor", true)->value();
    weatherTempTomorrowColor = hexToColor(weatherTempTomorrowColorHex);
  }

  if (request->hasParam("homeTitleColor", true)) {
    homeTitleColorHex = request->getParam("homeTitleColor", true)->value();
    homeTitleColor = hexToColor(homeTitleColorHex);
  }
  if (request->hasParam("homeTimeColor", true)) {
    homeTimeColorHex = request->getParam("homeTimeColor", true)->value();
    homeTimeColor = hexToColor(homeTimeColorHex);
  }
  if (request->hasParam("homePressureColor", true)) {
    homePressureColorHex = request->getParam("homePressureColor", true)->value();
    homePressureColor = hexToColor(homePressureColorHex);
  }
  if (request->hasParam("homeTempColor", true)) {
    homeTempColorHex = request->getParam("homeTempColor", true)->value();
    homeTempColor = hexToColor(homeTempColorHex);
  }
  if (request->hasParam("homeHumidityColor", true)) {
    homeHumidityColorHex = request->getParam("homeHumidityColor", true)->value();
    homeHumidityColor = hexToColor(homeHumidityColorHex);
  }

  if (request->hasParam("homeTempOffset", true)) {
    float v = request->getParam("homeTempOffset", true)->value().toFloat();
    if (v >= -10 && v <= 10) homeTempOffset = v;
  }
  if (request->hasParam("homePressureOffset", true)) {
    float v = request->getParam("homePressureOffset", true)->value().toFloat();
    if (v >= -50 && v <= 50) homePressureOffsetMm = v;
  }
  if (request->hasParam("homeHumidityOffset", true)) {
    float v = request->getParam("homeHumidityOffset", true)->value().toFloat();
    if (v >= -30 && v <= 30) homeHumidityOffset = v;
  }

  saveConfig();
  updateFullScrollText();
  request->redirect("/settings/weather");
}


// Разбирает "ЧЧ:ММ" из <input type="time"> в часы/минуты. Возвращает
// false, если строка пустая/некорректная - тогда часы/минуты не трогаем.
static bool parseHHMM(const String &value, int &hourOut, int &minuteOut) {
  int colonPos = value.indexOf(':');
  if (colonPos < 1) return false;
  hourOut = value.substring(0, colonPos).toInt();
  minuteOut = value.substring(colonPos + 1).toInt();
  return true;
}

void handleAlarmSettings(AsyncWebServerRequest *request) {
  String html = loadTemplate("/alarm.html");
  const char* dayField[7] = {"day0", "day1", "day2", "day3", "day4", "day5", "day6"};
  const char* timeField[7] = {"time0", "time1", "time2", "time3", "time4", "time5", "time6"};
  const char* soundField[7] = {"sound0", "sound1", "sound2", "sound3", "sound4", "sound5", "sound6"};

  for (int i = 0; i < 7; i++) {
    html.replace("{{" + String(dayField[i]) + "Checked}}", alarmEnabled[i] ? "checked" : "");
    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", alarmHour[i], alarmMinute[i]);
    html.replace("{{" + String(timeField[i]) + "}}", timeBuf);
    html.replace("{{" + String(soundField[i]) + "}}", alarmSoundPath[i]);
  }
  html.replace("{{autoStopMinutes}}", String(alarmAutoStopMinutes));
  html.replace("{{alarmVolume}}", String(alarmVolumePercent));
  sendHtmlChunked(request, html);
}

void handleAlarmSettingsPost(AsyncWebServerRequest *request) {
  const char* dayField[7] = {"day0", "day1", "day2", "day3", "day4", "day5", "day6"};
  const char* timeField[7] = {"time0", "time1", "time2", "time3", "time4", "time5", "time6"};
  const char* soundField[7] = {"sound0", "sound1", "sound2", "sound3", "sound4", "sound5", "sound6"};

  for (int i = 0; i < 7; i++) {
    alarmEnabled[i] = request->hasParam(dayField[i], true);
    if (request->hasParam(timeField[i], true)) {
      int h, m;
      if (parseHHMM(request->getParam(timeField[i], true)->value(), h, m)) {
        alarmHour[i] = h;
        alarmMinute[i] = m;
      }
    }
    if (request->hasParam(soundField[i], true)) {
      alarmSoundPath[i] = request->getParam(soundField[i], true)->value();
    }
  }
  if (request->hasParam("autoStop", true)) {
    int val = request->getParam("autoStop", true)->value().toInt();
    if (val >= 1 && val <= 30) alarmAutoStopMinutes = val;
  }
  if (request->hasParam("alarmVolume", true)) {
    int val = request->getParam("alarmVolume", true)->value().toInt();
    if (val >= 0 && val <= 100) alarmVolumePercent = val;
  }
  saveConfig();
  request->redirect("/settings/alarm");
}

void handleBigClockSettings(AsyncWebServerRequest *request) {
  String html = loadTemplate("/bigclock.html");
  for (int i = 0; i < BIG_FONT_COUNT; i++) {
    html.replace("{{selBig" + String(i) + "}}", (bigFontIndex == i) ? "selected" : "");
  }
  for (int i = 0; i < 5; i++) {
    html.replace("{{selAnim" + String(i) + "}}", (bigClockAnimMode == i) ? "selected" : "");
  }
  html.replace("{{scheduleChecked}}", bigClockScheduleEnabled ? "checked" : "");
  html.replace("{{bigClockColor}}", bigClockColorHex);
  char onBuf[6], offBuf[6];
  snprintf(onBuf, sizeof(onBuf), "%02d:%02d", bigClockOnHour, bigClockOnMinute);
  snprintf(offBuf, sizeof(offBuf), "%02d:%02d", bigClockOffHour, bigClockOffMinute);
  html.replace("{{onTime}}", onBuf);
  html.replace("{{offTime}}", offBuf);
  html.replace("{{activeChecked}}", bigClockActive ? "checked" : "");
  sendHtmlChunked(request, html);
}

void handleBigClockSettingsPost(AsyncWebServerRequest *request) {
  if (request->hasParam("font", true)) bigFontIndex = request->getParam("font", true)->value().toInt();
  if (request->hasParam("bigClockColor", true)) {
    bigClockColorHex = request->getParam("bigClockColor", true)->value();
    bigClockColor = hexToColor(bigClockColorHex);
  }
  if (request->hasParam("animDir", true)) bigClockAnimMode = request->getParam("animDir", true)->value().toInt();
  bigClockScheduleEnabled = request->hasParam("schedule", true);
  if (request->hasParam("onTime", true)) {
    int h, m;
    if (parseHHMM(request->getParam("onTime", true)->value(), h, m)) {
      bigClockOnHour = h; bigClockOnMinute = m;
    }
  }
  if (request->hasParam("offTime", true)) {
    int h, m;
    if (parseHHMM(request->getParam("offTime", true)->value(), h, m)) {
      bigClockOffHour = h; bigClockOffMinute = m;
    }
  }
  // "Включить/выключить сейчас" - принудительно, независимо от расписания.
  // Включаем ЧЕРЕЗ activateBigClock() (не напрямую bigClockActive = true),
  // иначе анимация выезда не запустится - только тогда, когда реально
  // ПЕРЕКЛЮЧАЕМ с выключенного на включённое (иначе каждое сохранение
  // формы с уже включённой галочкой заново запускало бы анимацию).
  bool wantActive = request->hasParam("activeNow", true);
  if (wantActive && !bigClockActive) {
    activateBigClock();
  } else if (!wantActive) {
    bigClockActive = false;
    bigClockAnimPlaying = false;
  }
  saveConfig();
  request->redirect("/settings/bigclock");
}

void handleNightSettings(AsyncWebServerRequest *request) {
  String html = loadTemplate("/night.html");
  html.replace("{{scheduleChecked}}", nightModeScheduleEnabled ? "checked" : "");
  char onBuf[6], offBuf[6];
  snprintf(onBuf, sizeof(onBuf), "%02d:%02d", nightOnHour, nightOnMinute);
  snprintf(offBuf, sizeof(offBuf), "%02d:%02d", nightOffHour, nightOffMinute);
  html.replace("{{onTime}}", onBuf);
  html.replace("{{offTime}}", offBuf);
  html.replace("{{nightSound}}", nightSoundPath);
  html.replace("{{morningSound}}", morningSoundPath);
  for (int i = 0; i < CLOCK_FONT_COUNT; i++) {
    html.replace("{{selNight" + String(i) + "}}", (nightFontIndex == i) ? "selected" : "");
  }
  html.replace("{{nightClockColor}}", nightClockColorHex);
  html.replace("{{nightLabelColor}}", nightLabelColorHex);
  html.replace("{{nightTempColor}}", nightTempColorHex);
  html.replace("{{nightScreenBrightness}}", String(nightScreenBrightnessPercent));
  sendHtmlChunked(request, html);
}

void handleNightSettingsPost(AsyncWebServerRequest *request) {
  nightModeScheduleEnabled = request->hasParam("schedule", true);
  if (request->hasParam("onTime", true)) {
    int h, m;
    if (parseHHMM(request->getParam("onTime", true)->value(), h, m)) {
      nightOnHour = h; nightOnMinute = m;
    }
  }
  if (request->hasParam("offTime", true)) {
    int h, m;
    if (parseHHMM(request->getParam("offTime", true)->value(), h, m)) {
      nightOffHour = h; nightOffMinute = m;
    }
  }
  if (request->hasParam("nightSound", true)) {
    nightSoundPath = request->getParam("nightSound", true)->value();
  }
  if (request->hasParam("morningSound", true)) {
    morningSoundPath = request->getParam("morningSound", true)->value();
  }
  if (request->hasParam("nightFont", true)) {
    int v = request->getParam("nightFont", true)->value().toInt();
    if (v >= 0 && v < CLOCK_FONT_COUNT) nightFontIndex = v;
  }
  if (request->hasParam("nightClockColor", true)) {
    nightClockColorHex = request->getParam("nightClockColor", true)->value();
    nightClockColor = hexToColor(nightClockColorHex);
  }
  if (request->hasParam("nightLabelColor", true)) {
    nightLabelColorHex = request->getParam("nightLabelColor", true)->value();
    nightLabelColor = hexToColor(nightLabelColorHex);
  }
  if (request->hasParam("nightTempColor", true)) {
    nightTempColorHex = request->getParam("nightTempColor", true)->value();
    nightTempColor = hexToColor(nightTempColorHex);
  }
  if (request->hasParam("nightScreenBrightness", true)) {
    int v = request->getParam("nightScreenBrightness", true)->value().toInt();
    if (v < 10) v = 10;
    if (v > 100) v = 100;
    nightScreenBrightnessPercent = (uint8_t)v;
  }
  saveConfig();
  request->redirect("/settings/night");
}

// ============================================================
// ВЕБ-РАДИО - страница управления + список станций
// ============================================================

void handleRadioSettings(AsyncWebServerRequest *request) {
  String html = loadTemplate("/radio.html");

  html.replace("{{scheduleChecked}}", radioScheduleEnabled ? "checked" : "");
  char onBuf[6], offBuf[6];
  snprintf(onBuf, sizeof(onBuf), "%02d:%02d", radioOnHour, radioOnMinute);
  snprintf(offBuf, sizeof(offBuf), "%02d:%02d", radioOffHour, radioOffMinute);
  html.replace("{{onTime}}", onBuf);
  html.replace("{{offTime}}", offBuf);
  html.replace("{{volume}}", String(getRadioVolumePercent()));

  if (radioState.active) {
    String status = radioState.stationName.length() ? radioState.stationName : "?";
    if (radioState.trackTitle.length()) status += " — " + radioState.trackTitle;
    if (radioState.bitrateKbps > 0) status += " (" + String(radioState.bitrateKbps) + " kbps)";
    html.replace("{{currentStatus}}", status);
  } else {
    html.replace("{{currentStatus}}", "не играет");
  }

  // Список станций - строим таблицу динамически (количество не
  // фиксировано, в отличие от 7 дней будильника - шаблонными
  // {{placeholder}} на каждую строку тут не обойтись)
  String rows = "";
  int count = getStationCount();
  for (int i = 0; i < count; i++) {
    StationInfo st = getStation(i);
    bool playing = radioState.active && radioState.stationIndex == i;
    rows += "<tr" + String(playing ? " class=\"playing\"" : "") + "><td>" + String(i + 1) + "</td><td>" + st.name + "</td>";
    rows += "<td><a href=\"/radio/play?index=" + String(i) + "\">▶</a></td>";
    rows += "<td><a href=\"/radio/delete?index=" + String(i) + "\" onclick=\"return confirm('Удалить станцию?')\">✕</a></td></tr>";
  }
  if (count == 0) {
    rows = "<tr><td colspan=\"4\">Список станций пуст - добавьте вручную или импортируйте плейлист ниже</td></tr>";
  }
  html.replace("{{stationRows}}", rows);

  // Цвета экрана радио. Если в radio.html есть поля {{radioClockColor}} - подставляем,
  // иначе (старый radio.html) добавляем форму в конец страницы
  if (html.indexOf("{{radioClockColor}}") >= 0) {
    html.replace("{{radioClockColor}}", radioClockColorHex);
    html.replace("{{radioTextColor}}", radioTextColorHex);
    html.replace("{{viewSel0}}", radioViewMode == 0 ? "selected" : "");
    html.replace("{{viewSel1}}", radioViewMode == 1 ? "selected" : "");
  } else {
    String colors = "<form method=\"POST\" action=\"/settings/radiocolors\" "
                    "style=\"margin:16px;padding:12px;border:1px solid #8886;border-radius:8px\">"
                    "<h3>Вид и цвета экрана радио</h3>"
                    "<p><label>Вид <select name=\"radioViewMode\"><option value=\"0\"" + String(radioViewMode == 0 ? " selected" : "") + ">Спектр</option>"
                    "<option value=\"1\"" + String(radioViewMode == 1 ? " selected" : "") + ">VU-метр (два канала)</option></select></label></p>"
                    "<p><label>Часы <input type=\"color\" name=\"radioClockColor\" value=\"" + radioClockColorHex + "\"></label></p>"
                    "<p><label>Бегущая строка <input type=\"color\" name=\"radioTextColor\" value=\"" + radioTextColorHex + "\"></label></p>"
                    "<button type=\"submit\">Сохранить цвета</button></form>";
    int bodyEnd = html.lastIndexOf("</body>");
    if (bodyEnd >= 0) html = html.substring(0, bodyEnd) + colors + html.substring(bodyEnd);
    else html += colors;
  }

  sendHtmlChunked(request, html);
}

void handleRadioColorsPost(AsyncWebServerRequest *request) {
  if (request->hasParam("radioViewMode", true)) {
    int m = request->getParam("radioViewMode", true)->value().toInt();
    radioViewMode = (uint8_t)(m == 1 ? 1 : 0);
  }
  if (request->hasParam("radioClockColor", true)) {
    String v = request->getParam("radioClockColor", true)->value();
    if (v.length() == 7 && v[0] == '#') {
      radioClockColorHex = v;
      radioClockColor = hexToColor(v);
    }
  }
  if (request->hasParam("radioTextColor", true)) {
    String v = request->getParam("radioTextColor", true)->value();
    if (v.length() == 7 && v[0] == '#') {
      radioTextColorHex = v;
      radioTextColor = hexToColor(v);
    }
  }
  saveConfig();
  request->redirect("/settings/radio");
}

void handleRadioSettingsPost(AsyncWebServerRequest *request) {
  radioScheduleEnabled = request->hasParam("schedule", true);
  if (request->hasParam("onTime", true)) {
    int h, m;
    if (parseHHMM(request->getParam("onTime", true)->value(), h, m)) {
      radioOnHour = h;
      radioOnMinute = m;
    }
  }
  if (request->hasParam("offTime", true)) {
    int h, m;
    if (parseHHMM(request->getParam("offTime", true)->value(), h, m)) {
      radioOffHour = h;
      radioOffMinute = m;
    }
  }
  saveConfig();
  request->redirect("/settings/radio");
}

void handleRadioPlay(AsyncWebServerRequest *request) {
  if (request->hasParam("index")) {
    startRadio(request->getParam("index")->value().toInt());
  }
  request->redirect("/settings/radio");
}

void handleRadioStop(AsyncWebServerRequest *request) {
  stopRadio();
  request->redirect("/settings/radio");
}

void handleRadioNext(AsyncWebServerRequest *request) {
  nextRadioStation();
  request->redirect("/settings/radio");
}

void handleRadioPrev(AsyncWebServerRequest *request) {
  prevRadioStation();
  request->redirect("/settings/radio");
}

void handleRadioVolume(AsyncWebServerRequest *request) {
  if (request->hasParam("value", true)) {
    int v = request->getParam("value", true)->value().toInt();
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    setRadioVolumePercent((uint8_t)v);
  }
  request->redirect("/settings/radio");
}

void handleRadioAddStation(AsyncWebServerRequest *request) {
  if (request->hasParam("name", true) && request->hasParam("url", true)) {
    addStation(request->getParam("name", true)->value(), request->getParam("url", true)->value());
  }
  request->redirect("/settings/radio");
}

void handleRadioDeleteStation(AsyncWebServerRequest *request) {
  if (request->hasParam("index")) {
    removeStation(request->getParam("index")->value().toInt());
  }
  request->redirect("/settings/radio");
}

// Импорт плейлиста - вставляется ТЕКСТОМ в textarea (не файлом), чтобы
// не связываться с multipart file upload в AsyncWebServer. Пользователь
// просто открывает свой .m3u/.pls в блокноте и вставляет содержимое.
// Импорт плейлиста - настоящая загрузка файла (multipart/form-data),
// а не вставка текста в поле. Файл может прийти несколькими кусками
// (index/len/final) - собираем всё в буфер и разбираем целиком, когда
// пришёл последний кусок.
static String radioUploadBuffer;

void handleRadioImportUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (index == 0) {
    radioUploadBuffer = "";
    Serial.println("📥 Приём файла плейлиста: " + filename);
  }
  radioUploadBuffer.concat((const char *)data, len);
  if (final) {
    Serial.println("📥 Файл получен целиком (" + String(radioUploadBuffer.length()) + " байт), разбираю...");
    Serial.println("--- ДИАГНОСТИКА: первые 150 символов буфера (как есть) ---");
    Serial.println(radioUploadBuffer.substring(0, 150));
    Serial.println("--- ДИАГНОСТИКА: та же часть побайтово (hex) ---");
    for (int i = 0; i < 60 && i < (int)radioUploadBuffer.length(); i++) {
      Serial.printf("%02X ", (uint8_t)radioUploadBuffer[i]);
    }
    Serial.println();
    Serial.println("--- конец диагностики ---");
    importPlaylist(radioUploadBuffer);
    radioUploadBuffer = "";
  }
}

// ============================================================
// ИНТЕГРАЦИИ (MQTT / Народный мониторинг) - страница /settings/integrations
// ============================================================
static String htmlEsc(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char ch = s[i];
    if (ch == '&') o += "&amp;";
    else if (ch == '<') o += "&lt;";
    else if (ch == '>') o += "&gt;";
    else if (ch == '"') o += "&quot;";
    else o += ch;
  }
  return o;
}

void handleIntegrationsSettings(AsyncWebServerRequest *request) {
  String html = loadTemplate("/integrations.html");
  IntegrationsConfig c = getIntegrationsConfig();

  html.replace("{{chkMqtt}}", c.mqttEnabled ? "checked" : "");
  html.replace("{{selMode0}}", c.mqttMode == 0 ? "selected" : "");
  html.replace("{{selMode1}}", c.mqttMode == 1 ? "selected" : "");
  html.replace("{{mqttHost}}", htmlEsc(c.mqttHost));
  html.replace("{{mqttPort}}", String(c.mqttPort));
  html.replace("{{mqttUser}}", htmlEsc(c.mqttUser));
  html.replace("{{passHint}}", c.mqttPass.length() ? "пароль сохранён" : "не задан");
  html.replace("{{mqttBase}}", htmlEsc(c.mqttBase));
  html.replace("{{mqttInterval}}", String(c.mqttInterval));
  html.replace("{{chkMqttHome}}", c.mqttHome ? "checked" : "");
  html.replace("{{chkMqttOut}}", c.mqttOutdoor ? "checked" : "");
  html.replace("{{chkMqttWx}}", c.mqttWeather ? "checked" : "");
  html.replace("{{chkMqttSys}}", c.mqttStatus ? "checked" : "");

  html.replace("{{chkNm}}", c.nmEnabled ? "checked" : "");
  html.replace("{{nmName}}", htmlEsc(c.nmName));
  html.replace("{{chkNmHome}}", c.nmHome ? "checked" : "");
  html.replace("{{chkNmOut}}", c.nmOutdoor ? "checked" : "");
  html.replace("{{mac}}", integrationsMac());

  html.replace("{{mqttStatus}}", htmlEsc(integrationsMqttStatus()));
  html.replace("{{nmStatus}}", htmlEsc(integrationsNarodmonStatus()));

  sendHtmlChunked(request, html);
}

void handleIntegrationsSettingsPost(AsyncWebServerRequest *request) {
  IntegrationsConfig c = getIntegrationsConfig();
  auto P = [&](const char *name) -> String {
    return request->hasParam(name, true) ? request->getParam(name, true)->value() : String("");
  };

  c.mqttEnabled = request->hasParam("mqttEnabled", true);
  if (request->hasParam("mqttMode", true)) c.mqttMode = P("mqttMode").toInt();
  c.mqttHost = P("mqttHost");
  c.mqttHost.trim();
  int port = P("mqttPort").toInt();
  if (port > 0 && port < 65536) c.mqttPort = port;
  c.mqttUser = P("mqttUser");
  String pass = P("mqttPass");
  if (pass.length()) c.mqttPass = pass;
  if (request->hasParam("clearPass", true)) c.mqttPass = "";
  c.mqttBase = P("mqttBase");
  int iv = P("mqttInterval").toInt();
  if (iv >= 10) c.mqttInterval = iv;
  c.mqttHome = request->hasParam("mqttHome", true);
  c.mqttOutdoor = request->hasParam("mqttOutdoor", true);
  c.mqttWeather = request->hasParam("mqttWeather", true);
  c.mqttStatus = request->hasParam("mqttStatus", true);

  c.nmEnabled = request->hasParam("nmEnabled", true);
  c.nmName = P("nmName");
  c.nmName.trim();
  c.nmHome = request->hasParam("nmHome", true);
  c.nmOutdoor = request->hasParam("nmOutdoor", true);

  setIntegrationsConfig(c);
  request->redirect("/settings/integrations");
}

// ============================================================
// РЕЗЕРВНАЯ КОПИЯ НАСТРОЕК: скачать /config.json и восстановить из файла.
// Нужна, потому что "Upload Filesystem Image" стирает config.json.
// Внимание: в файле открытым текстом лежат пароль Wi-Fi и ключ OpenWeatherMap.
// ============================================================
static void scheduleRestart(uint32_t ms) {
  xTaskCreate([](void *p) {
    vTaskDelay(pdMS_TO_TICKS((uint32_t)(uintptr_t)p));
    ESP.restart();
  }, "restart", 2048, (void *)(uintptr_t)ms, 1, nullptr);
}

void handleBackupDownload(AsyncWebServerRequest *request) {
  if (!LittleFS.exists("/config.json")) {
    request->send(404, "text/plain; charset=utf-8", "config.json not found");
    return;
  }
  AsyncWebServerResponse *resp = request->beginResponse(LittleFS, "/config.json", "application/json");
  resp->addHeader("Content-Disposition", "attachment; filename=\"pixelhub75-config.json\"");
  request->send(resp);
}

static String restoreBuffer;
static int restoreStatus = 0;   // 0 - ничего, 1 - файл принят и записан, 2 - ошибка
static String restoreError;

void handleBackupRestoreUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (index == 0) {
    restoreBuffer = "";
    restoreStatus = 0;
    restoreError = "";
    Serial.println("📥 Приём файла настроек: " + filename);
  }
  if (restoreStatus == 2) return;   // уже отказали, остаток файла просто пропускаем
  if (restoreBuffer.length() + len > 24576) {
    restoreStatus = 2;
    restoreError = "файл слишком большой для файла настроек";
    restoreBuffer = "";
    return;
  }
  restoreBuffer.concat((const char *)data, len);
  if (final) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, restoreBuffer);
    if (err) {
      restoreStatus = 2;
      restoreError = "файл не является корректным JSON";
    } else if (!doc["clockFontIndex"].is<int>() || !doc["brightness"].is<int>()) {
      restoreStatus = 2;
      restoreError = "это не файл настроек PixelHub 75";
    } else {
      File f = LittleFS.open("/config.json", "w");
      if (!f) {
        restoreStatus = 2;
        restoreError = "не удалось записать config.json";
      } else {
        f.print(restoreBuffer);
        f.close();
        restoreStatus = 1;
        Serial.println("✅ config.json восстановлен из файла (" + String(restoreBuffer.length()) + " байт)");
      }
    }
    restoreBuffer = "";
  }
}

void handleBackupRestoreDone(AsyncWebServerRequest *request) {
  const char *head = "<!DOCTYPE html><html lang='ru'><head><meta charset='utf-8'>"
                     "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                     "<title>PixelHub 75</title><link rel='stylesheet' href='/style.css'></head><body><div class='wrap'><div class='card'>";
  if (restoreStatus == 1) {
    String html = String(head) +
      "<h1>Настройки восстановлены</h1><p>Устройство перезагружается, страница откроется через несколько секунд.</p>"
      "<meta http-equiv='refresh' content='12;url=/'></div></div></body></html>";
    request->send(200, "text/html; charset=utf-8", html);
    scheduleRestart(1500);
  } else {
    String why = restoreError.length() ? restoreError : String("файл не получен");
    String html = String(head) +
      "<h1>Не удалось восстановить</h1><p>" + htmlEsc(why) + ".</p>"
      "<p><a href='/settings/integrations'>Назад</a></p></div></div></body></html>";
    request->send(400, "text/html; charset=utf-8", html);
  }
}

// ============================================================
// ПИНЫ МАТРИЦЫ: просмотр и изменение без пересборки прошивки
// ============================================================
static String pinsPageHtml(const MatrixPins &p, const String &msg) {
  struct Row { const char *name; const char *title; int val; };
  const Row rows[14] = {
    {"r1",  "R1 (красный, верх)",   p.r1},
    {"g1",  "G1 (зелёный, верх)",   p.g1},
    {"b1",  "B1 (синий, верх)",     p.b1},
    {"r2",  "R2 (красный, низ)",    p.r2},
    {"g2",  "G2 (зелёный, низ)",    p.g2},
    {"b2",  "B2 (синий, низ)",      p.b2},
    {"a",   "A (адрес)",            p.a},
    {"b",   "B (адрес)",            p.b},
    {"c",   "C (адрес)",            p.c},
    {"d",   "D (адрес)",            p.d},
    {"e",   "E (адрес, -1 если нет)", p.e},
    {"lat", "LAT (защёлка)",        p.lat},
    {"oe",  "OE (включение)",       p.oe},
    {"clk", "CLK (такт)",           p.clk}
  };
  String h;
  h.reserve(3000);
  h += "<!DOCTYPE html><html lang='ru'><head><meta charset='utf-8'>"
       "<meta name='viewport' content='width=device-width, initial-scale=1'>"
       "<title>Пины матрицы — PixelHub 75</title><link rel='stylesheet' href='/style.css'></head><body>"
       "<div class='wrap'><div class='nav'><a href='/'>Главная</a>"
       "<a href='/settings/sensors'>Датчики</a><a href='/settings/brightness'>Яркость</a>"
       "<a href='/settings/wifi'>Wi-Fi</a></div>"
       "<h1>Пины матрицы</h1>";
  if (msg.length()) h += "<div class='card'><b>" + msg + "</b></div>";
  h += "<div class='card'><p class='hint' style='margin-top:0'>Допустимые GPIO классического ESP32: "
       "0-5, 12-33, 40-48. Нельзя 6-11 (флеш-память) и 34-39 (только вход). "
       "Значения по умолчанию — из config_pins.h. После сохранения плата перезагрузится.</p>"
       "<form method='POST' action='/settings/pins'>";
  for (const Row &r : rows) {
    h += "<label for='"; h += r.name; h += "'>"; h += r.title; h += "</label>";
    h += "<input type='number' id='"; h += r.name; h += "' name='"; h += r.name;
    h += "' min='-1' max='48' value='"; h += String(r.val); h += "'>";
  }
  h += "</form>"
       "<p class='hint'><a href='/settings/pins/reset'>Сбросить к значениям по умолчанию</a></p>"
       "</div></div></body></html>";
  return h;
}

static void handlePinsSettings(AsyncWebServerRequest *request) {
  String msg = request->hasParam("msg") ? request->getParam("msg")->value() : String("");
  if (msg == "reset") msg = "Сброшено к значениям по умолчанию";
  sendHtmlChunked(request, pinsPageHtml(pinsLoad(), msg));
}

static void handlePinsSettingsPost(AsyncWebServerRequest *request) {
  MatrixPins p;
  auto val = [&](const char *n) -> int {
    if (!request->hasParam(n, true)) return 0;
    return request->getParam(n, true)->value().toInt();
  };
  p.r1 = val("r1");   p.g1 = val("g1");   p.b1 = val("b1");
  p.r2 = val("r2");   p.g2 = val("g2");   p.b2 = val("b2");
  p.a  = val("a");    p.b  = val("b");    p.c  = val("c");
  p.d  = val("d");    p.e  = val("e");
  p.lat = val("lat"); p.oe = val("oe");   p.clk = val("clk");
  String err;
  if (!pinsValidate(p, err)) {
    sendHtmlChunked(request, pinsPageHtml(pinsLoad(), String("Не сохранено: ") + err));
    return;
  }
  pinsSave(p);
  String html = "<!DOCTYPE html><html lang='ru'><head><meta charset='utf-8'>"
                "<title>Пины сохранены</title><link rel='stylesheet' href='/style.css'></head><body>"
                "<div class='wrap'><div class='card'><h2>Пины сохранены</h2>"
                "<p class='hint'>Плата перезагружается и поднимет матрицу с новыми пинами.</p></div></div>"
                "<script>setTimeout(function(){location='/';}, 5000);</script></body></html>";
  sendHtmlChunked(request, html);
  scheduleRestart(1200);
}

void setupWebServer() {


  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *r) {
    AsyncWebServerResponse *res = r->beginResponse_P(200, "image/png", FAVICON_PNG, FAVICON_PNG_len);
    res->addHeader("Cache-Control", "public, max-age=86400");
    r->send(res);
  });
  server.on("/favicon.png", HTTP_GET, [](AsyncWebServerRequest *r) {
    AsyncWebServerResponse *res = r->beginResponse_P(200, "image/png", FAVICON_PNG, FAVICON_PNG_len);
    res->addHeader("Cache-Control", "public, max-age=86400");
    r->send(res);
  });
  server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *r) {
    AsyncWebServerResponse *res = r->beginResponse_P(200, "image/png", LOGO_PNG, LOGO_PNG_len);
    res->addHeader("Cache-Control", "public, max-age=86400");
    r->send(res);
  });
  server.on("/settings/alarm", HTTP_GET, [](AsyncWebServerRequest *r) {
    static const char *dn[7] = {"Вс","Пн","Вт","Ср","Чт","Пт","Сб"};
    String h = "<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
      "<title>Будильник — PixelHub 75</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body><div class=\"wrap\">"
      "<div class=\"nav\"><a href=\"/\">Главная</a><a href=\"/settings/clock\">Часы</a></div>"
      "<h1>Будильник</h1><div class=\"card\"><p class=\"hint\" style=\"margin-top:0\">Мелодии: папка /02, файлы 001.mp3…009.mp3.</p>"
      "<form method=\"POST\" action=\"/settings/alarm\">";
    for (int i = 0; i < 7; i++) {
      int tr = 1;
      String s = alarmSoundPath[i]; s.trim();
      int sl = s.lastIndexOf('/'); if (sl >= 0) s = s.substring(sl + 1);
      int dt = s.indexOf('.'); if (dt >= 0) s = s.substring(0, dt);
      if (s.length() && s.toInt() > 0) tr = constrain(s.toInt(), 1, 9);
      h += "<div class=\"line-title\" style=\"margin-top:10px\">" + String(dn[i]) + "</div>"
        "<div style=\"display:flex;gap:6px;flex-wrap:wrap\">"
        "<input type=\"number\" name=\"h" + String(i) + "\" min=\"0\" max=\"23\" value=\"" + String(alarmHour[i]) + "\" style=\"width:70px\">"
        "<input type=\"number\" name=\"m" + String(i) + "\" min=\"0\" max=\"59\" value=\"" + String(alarmMinute[i]) + "\" style=\"width:70px\">"
        "<input type=\"number\" name=\"t" + String(i) + "\" min=\"1\" max=\"9\" value=\"" + String(tr) + "\" style=\"width:70px\">"
        "<label style=\"margin:0\"><input type=\"checkbox\" name=\"e" + String(i) + "\"";
      if (alarmEnabled[i]) h += " checked";
      h += "> вкл</label></div>";
    }
    h += "<button type=\"submit\" style=\"margin-top:14px\">Сохранить</button></form>"
      "<p class=\"hint\"><a href=\"/api/alarm?stop=1\">Остановить будильник</a></p></div></div></body></html>";
    r->send(200, "text/html; charset=utf-8", h);
  });
  server.on("/settings/alarm", HTTP_POST, [](AsyncWebServerRequest *r) {
    for (int i = 0; i < 7; i++) {
      String hs = "h" + String(i), ms = "m" + String(i), ts = "t" + String(i), es = "e" + String(i);
      if (r->hasParam(hs, true)) alarmHour[i] = constrain(r->getParam(hs, true)->value().toInt(), 0, 23);
      if (r->hasParam(ms, true)) alarmMinute[i] = constrain(r->getParam(ms, true)->value().toInt(), 0, 59);
      if (r->hasParam(ts, true)) alarmSoundPath[i] = String(constrain(r->getParam(ts, true)->value().toInt(), 1, 9));
      alarmEnabled[i] = r->hasParam(es, true);
    }
    saveConfig();
    r->redirect("/settings/clock");
  });

  server.on("/api/alarm", HTTP_GET, [](AsyncWebServerRequest *r) {
    if (r->hasParam("stop")) alarmDfpStop();
    r->send(200, "application/json", String("{\"active\":") + (alarmActive ? 1 : 0) + "}");
  });

  server.on("/settings/hwpins", HTTP_GET, [](AsyncWebServerRequest *r) {
    String h = "<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
      "<title>Пины датчиков — PixelHub 75</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body><div class=\"wrap\">"
      "<div class=\"nav\"><a href=\"/\">Главная</a><a href=\"/settings/clock\">Часы</a><a href=\"/settings/pins\">Пины матрицы</a></div>"
      "<h1>Пины датчиков и плеера</h1><div class=\"card\">"
      "<form method=\"POST\" action=\"/settings/hwpins\">"
      "<label for=\"od\">DS18B20, DATA</label><input type=\"number\" id=\"od\" name=\"od\" min=\"0\" max=\"39\" value=\"" + String(outdoorPin) + "\">"
      "<label for=\"tx\">DFPlayer TX (платы)</label><input type=\"number\" id=\"tx\" name=\"tx\" min=\"0\" max=\"39\" value=\"" + String(dfpTxPin) + "\">"
      "<label for=\"rx\">DFPlayer RX (платы)</label><input type=\"number\" id=\"rx\" name=\"rx\" min=\"0\" max=\"39\" value=\"" + String(dfpRxPin) + "\">"
      "<p class=\"hint\">После сохранения плата перезагрузится. Нельзя 6–11 (флеш-память).</p>"
      "<button type=\"submit\">Сохранить и перезагрузить</button></form></div></div></body></html>";
    r->send(200, "text/html; charset=utf-8", h);
  });

  server.on("/settings/hwpins", HTTP_POST, [](AsyncWebServerRequest *r) {
    auto P = [&](const char *n, int def) -> int {
      return r->hasParam(n, true) ? constrain(r->getParam(n, true)->value().toInt(), 0, 39) : def;
    };
    outdoorPin = P("od", outdoorPin);
    dfpTxPin = P("tx", dfpTxPin);
    dfpRxPin = P("rx", dfpRxPin);
    saveConfig();
    r->send(200, "text/html; charset=utf-8",
      "<!DOCTYPE html><html><head><meta charset='utf-8'><meta http-equiv='refresh' content='5;url=/'></head>"
      "<body style='font-family:sans-serif'><h2>Сохранено</h2><p>Плата перезагружается…</p></body></html>");
    xTaskCreate([](void *) { vTaskDelay(pdMS_TO_TICKS(800)); ESP.restart(); }, "pinsrb", 2048, nullptr, 1, nullptr);
  });

  server.on("/settings/sound", HTTP_GET, [](AsyncWebServerRequest *r) {
    String h = "<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
      "<title>Звук — PixelHub 75</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body><div class=\"wrap\">"
      "<div class=\"nav\"><a href=\"/\">Главная</a><a href=\"/settings/clock\">Часы</a></div>"
      "<h1>Звук</h1><div class=\"card\"><p class=\"hint\">";
    h += dfpReady() ? "DFPlayer подключён." : "DFPlayer не найден.";
    h += " Файлов: " + String(dfpTracks()) + ".</p>"
      "<form method=\"POST\" action=\"/settings/sound\">"
      "<label for=\"vol\">Громкость (0–30)</label><input type=\"number\" id=\"vol\" name=\"vol\" min=\"0\" max=\"30\" value=\"" + String(dfVolume) + "\">"
      "<label for=\"voice\">Голос</label><select id=\"voice\" name=\"voice\">"
      "<option value=\"0\"" + String(hourlyVoiceMode == 0 ? " selected" : "") + ">Женский</option>"
      "<option value=\"1\"" + String(hourlyVoiceMode == 1 ? " selected" : "") + ">Мужской</option>"
      "<option value=\"2\"" + String(hourlyVoiceMode == 2 ? " selected" : "") + ">Куранты</option></select>"
      "<div class=\"checkbox-row\"><input type=\"checkbox\" id=\"hourly\" name=\"hourly\"";
    if (hourlyAnnounceEnabled) h += " checked";
    h += "><label for=\"hourly\" style=\"margin:0\">Озвучивать время каждый час</label></div>"
      "<div class=\"checkbox-row\"><input type=\"checkbox\" id=\"night\" name=\"night\"";
    if (dfNightSound) h += " checked";
    h += "><label for=\"night\" style=\"margin:0\">Ночной и утренний звук</label></div>"
      "<button type=\"submit\">Сохранить</button></form>"
      "<p class=\"hint\"><a href=\"/api/df?folder=1&file=1\">Ночной</a> | <a href=\"/api/df?folder=1&file=2\">Утренний</a> | "
      "<a href=\"/api/df?folder=12&file=" + String(hours + 1) + "\">Куранты</a> | <a href=\"/api/df?stop=1\">Стоп</a></p></div></div></body></html>";
    r->send(200, "text/html; charset=utf-8", h);
  });
  server.on("/settings/sound", HTTP_POST, [](AsyncWebServerRequest *r) {
    if (r->hasParam("vol", true)) dfpSetVolume((uint8_t)constrain(r->getParam("vol", true)->value().toInt(), 0, 30));
    dfNightSound = r->hasParam("night", true);
    hourlyAnnounceEnabled = r->hasParam("hourly", true);
    if (r->hasParam("voice", true)) hourlyVoiceMode = constrain(r->getParam("voice", true)->value().toInt(), 0, 2);
    saveConfig();
    r->redirect("/settings/clock");
  });

  server.on("/settings/air", HTTP_POST, [](AsyncWebServerRequest *req) {
    auto P = [&](const char *n, const String &def) -> String {
      return req->hasParam(n, true) ? req->getParam(n, true)->value() : def;
    };
    airTitleColorHex = P("airTitle", airTitleColorHex);
    airCo2ColorHex = P("airCo2", airCo2ColorHex);
    airTvocColorHex = P("airTvoc", airTvocColorHex);
    airBarColorHex = P("airBar", airBarColorHex);
    airTitleColor = hexToColor(airTitleColorHex);
    airCo2Color = hexToColor(airCo2ColorHex);
    airTvocColor = hexToColor(airTvocColorHex);
    airBarColor = hexToColor(airBarColorHex);
    saveConfig();
    req->redirect("/settings/weather");
  });

  server.on("/settings/pins", HTTP_GET, handlePinsSettings);
  server.on("/settings/pins", HTTP_POST, handlePinsSettingsPost);
  server.on("/settings/pins/reset", HTTP_GET, [](AsyncWebServerRequest *r) {
    pinsReset();
    r->redirect("/settings/pins?msg=reset");
  });
  server.on("/api/df", HTTP_GET, [](AsyncWebServerRequest *r) {
    if (r->hasParam("vol")) dfpSetVolume((uint8_t)constrain(r->getParam("vol")->value().toInt(), 0, 30));
    int track = r->hasParam("track") ? r->getParam("track")->value().toInt() : -1;
    if (track >= 0) dfpPlayTrack((uint16_t)track);
    if (r->hasParam("folder") && r->hasParam("file"))
      dfpPlayFolder((uint8_t)r->getParam("folder")->value().toInt(), (uint8_t)r->getParam("file")->value().toInt());
    if (r->hasParam("stop")) dfpStop();
    String j = "{\"ok\":" + String(dfpReady() ? 1 : 0) +
               ",\"vol\":" + String(dfpVolume()) +
               ",\"tracks\":" + String(dfpTracks()) + "}";
    r->send(200, "application/json", j);
  });

  server.on("/api/home", HTTP_GET, [](AsyncWebServerRequest *r) {
    String j = "{\"line3\":\"" + scrollLineText[3] + "\",\"detected\":" + String((int)detectedSensor) +
               ",\"wifi\":" + String(homeSourceActive ? 1 : 0) +
               ",\"temp\":" + String(homeTemp, 2) +
               ",\"hum\":" + String(homeHumidity, 1) +
               ",\"press\":" + String(homePressure, 1) + "}";
    r->send(200, "application/json", j);
  });

  server.on("/api/reboot", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/plain", "rebooting");
    xTaskCreate([](void *) { vTaskDelay(pdMS_TO_TICKS(500)); ESP.restart(); }, "rb", 2048, nullptr, 1, nullptr);
  });

  server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "application/json", String("{\"built\":\"") + __DATE__ + " " + __TIME__ + "\"}");
  });

  server.on("/api/heap", HTTP_GET, [](AsyncWebServerRequest *r) {
    AsyncResponseStream *res = r->beginResponseStream("application/json");
    if (!res) { r->send(500); return; }
    res->printf("{\"free\":%u,\"min\":%u,\"maxblock\":%u,\"psram\":%u}",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                (unsigned)ESP.getFreePsram());
    r->send(res);
  });
  server.on("/", HTTP_GET, handleRoot);
  server.on("/settings/clock", HTTP_GET, handleClockSettings);
  server.on("/settings/clock", HTTP_POST, handleClockSettingsPost);
  server.on("/settings/scroll", HTTP_GET, handleScrollSettings);
  server.on("/settings/scroll", HTTP_POST, handleScrollSettingsPost);
  server.on("/settings/wifi", HTTP_GET, handleWifiSettings);
  server.on("/settings/wifi", HTTP_POST, handleWifiSettingsPost);
  server.on("/settings/brightness", HTTP_GET, handleBrightnessSettings);
  server.on("/settings/brightness", HTTP_POST, handleBrightnessSettingsPost);
  server.on("/settings/weather", HTTP_GET, handleWeatherSettings);
  server.on("/settings/weather", HTTP_POST, handleWeatherSettingsPost);


  server.on("/settings/bigclock", HTTP_GET, handleBigClockSettings);
  server.on("/settings/bigclock", HTTP_POST, handleBigClockSettingsPost);
  server.on("/settings/night", HTTP_GET, handleNightSettings);
  server.on("/settings/night", HTTP_POST, handleNightSettingsPost);












  server.on("/settings/integrations", HTTP_GET, handleIntegrationsSettings);
  server.on("/settings/integrations", HTTP_POST, handleIntegrationsSettingsPost);
  server.on("/backup/download", HTTP_GET, handleBackupDownload);
  server.on("/backup/restore", HTTP_POST, handleBackupRestoreDone, handleBackupRestoreUpload);

  // Общий CSS для всех страниц веб-интерфейса (лежит в data/style.css)
  server.serveStatic("/style.css", LittleFS, "/style.css");

  server.begin();
  Serial.println("✅ Веб-сервер запущен");
}
