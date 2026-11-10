#include "ota_update.h"
#include "web_send.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <ESPAsyncWebServer.h>
#include "radio.h"
#include "audio_engine.h"
#include <HTTPClient.h>
#include <WiFiClient.h>

// ------------------------------------------------------------
// Состояние текущей загрузки (одна загрузка за раз)
// ------------------------------------------------------------
static void handleOtaUrl(AsyncWebServerRequest *req);
static bool otaAuthOk = false;     // пароль верный
static bool otaIsFs = false;       // true - файловая система, false - прошивка
static bool otaStarted = false;    // Update.begin() прошёл, идёт запись
static bool otaFinished = false;   // Update.end() успешно завершён
static String otaError;            // причина отказа (пусто - ошибок нет)
static size_t otaReceived = 0;     // принято байт

static void otaScheduleRestart(uint32_t ms) {
  xTaskCreate([](void *p) {
    vTaskDelay(pdMS_TO_TICKS((uint32_t)(uintptr_t)p));
    ESP.restart();
  }, "otarestart", 2048, (void *)(uintptr_t)ms, 1, nullptr);
}

static String otaHtmlEsc(const String &s) {
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

// Файл приходит кусками (index/len/final) - пишем каждый кусок сразу во флеш-память,
// в оперативной памяти целый файл не держим (прошивка больше свободной RAM).
static void handleOtaUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (index == 0) {
    otaStarted = false;
    otaFinished = false;
    otaError = "";
    otaReceived = 0;
    otaAuthOk = request->hasParam("pass", true) && request->getParam("pass", true)->value() == OTA_PASSWORD;
    otaIsFs = request->hasParam("kind", true) && request->getParam("kind", true)->value() == "fs";
    if (!otaAuthOk) {
      otaError = "неверный пароль";
      Serial.println("⛔ Обновление отклонено: неверный пароль");
      return;
    }
    Serial.println("📥 Обновление " + String(otaIsFs ? "файловой системы" : "прошивки") + ": " + filename);
    // Звук на время записи останавливаем: запись во флеш-память иначе даёт щелчки
    stopRadio();
    stopAudio();
    if (otaIsFs) LittleFS.end();   // размонтируем перед перезаписью раздела
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, otaIsFs ? U_SPIFFS : U_FLASH)) {
      otaError = String("не удалось начать обновление: ") + Update.errorString();
      Serial.println("❌ " + otaError);
      if (otaIsFs) LittleFS.begin(true);
      return;
    }
    otaStarted = true;
  }
  if (!otaStarted) return;   // уже отказали - остаток файла пропускаем

  if (Update.write(data, len) != len) {
    otaError = String("ошибка записи: ") + Update.errorString();
    Serial.println("❌ " + otaError);
    Update.abort();
    otaStarted = false;
    if (otaIsFs) LittleFS.begin(true);
    return;
  }
  otaReceived += len;

  if (final) {
    otaStarted = false;
    if (Update.end(true)) {
      otaFinished = true;
      Serial.println("✅ Обновление записано (" + String(otaReceived) + " байт), перезагрузка");
    } else {
      otaError = String("файл не прошёл проверку: ") + Update.errorString() +
                 (otaIsFs ? "" : " (нужен именно firmware.bin)");
      Serial.println("❌ " + otaError);
      if (otaIsFs) LittleFS.begin(true);
    }
  }
}

static const char OTA_PAGE_HEAD[] =
  "<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\">"
  "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
  "<title>Обновление — PixelHub 75</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body><div class=\"wrap\">";

static void handleOtaDone(AsyncWebServerRequest *request) {
  String html = OTA_PAGE_HEAD;
  int code = 200;
  if (otaFinished && otaError.length() == 0) {
    html += "<div class=\"card\"><h1>Обновление записано</h1>"
            "<p>Устройство перезагружается. Страница откроется сама через ~20 секунд.</p>"
            "<meta http-equiv=\"refresh\" content=\"20;url=/\"></div></div></body></html>";
  } else {
    code = otaAuthOk ? 400 : 403;
    String why = otaError.length() ? otaError : String("файл не получен");
    html += "<div class=\"card\"><h1>Обновление не выполнено</h1><p>" + otaHtmlEsc(why) + ".</p>"
            "<p>Текущая прошивка не изменена, устройство работает как раньше.</p>"
            "<p><a href=\"/settings/update\">Назад</a></p></div></div></body></html>";
  }
  AsyncWebServerResponse *resp = request->beginResponse(code, "text/html; charset=utf-8", html);
  resp->addHeader("Connection", "close");
  request->send(resp);
  if (code == 200) otaScheduleRestart(1500);
}

static String buildOtaPage() {
  String h;
  h.reserve(5500);
  h += OTA_PAGE_HEAD;
  h += "<div class=\"nav\"><a href=\"/\">Главная</a><a href=\"/settings/clock\">Часы</a><a href=\"/settings/scroll\">Бегущая строка</a><a href=\"/settings/calendar\">Календарь</a>"
       "<a href=\"/settings/weather\">Погода</a><a href=\"/settings/bigclock\">Большие часы</a>"
       "<a href=\"/settings/night\">Ночь/утро</a><a href=\"/settings/integrations\">Интеграции</a>"
       "<a href=\"/settings/brightness\">Яркость</a><a href=\"/settings/wifi\">Wi-Fi</a></div>";
  h += "<h1>Обновление прошивки</h1>";

  const esp_partition_t *run = esp_ota_get_running_partition();
  h += "<div class=\"card\"><div class=\"line-title\">Текущая версия</div><p class=\"hint\">Сборка: <b>";
  h += String(__DATE__) + " " + String(__TIME__);
  h += "</b><br>Размер прошивки: <b>" + String(ESP.getSketchSize() / 1024) + " КБ</b>, свободно под новую: <b>" +
       String(ESP.getFreeSketchSpace() / 1024) + " КБ</b><br>Работает из слота: <b>" +
       String(run ? run->label : "?") + "</b></p></div>";

  h += "<div class=\"card\"><div class=\"line-title\">Прошивка (firmware.bin)</div>"
       "<p class=\"hint\">В PlatformIO нажмите «Build», затем возьмите файл "
       "<b>.pio/build/esp32dev/firmware.bin</b>. Настройки при обновлении сохраняются.</p>"
       "<form class=\"otaform\" method=\"POST\" action=\"/settings/update\" enctype=\"multipart/form-data\">"
       "<input type=\"hidden\" name=\"kind\" value=\"fw\">"
       "<label>Пароль обновления</label><input type=\"password\" name=\"pass\" autocomplete=\"off\" required>"
       "<label>Файл firmware.bin</label><input type=\"file\" name=\"file\" accept=\".bin\" required>"
       "<button type=\"submit\">Обновить прошивку</button></form></div>";

  h += "<div class=\"card\"><div class=\"line-title\">Страницы веб-интерфейса (littlefs.bin)</div>"
       "<p class=\"hint\">Нужно, только если в новой версии изменились html-страницы. В PlatformIO: «Build Filesystem Image», "
       "файл <b>.pio/build/esp32dev/littlefs.bin</b>. Настройки сохраняются, но список радиостанций "
       "и другие файлы из файловой системы (например stations.json) будут стёрты.</p>"
       "<form class=\"otaform\" method=\"POST\" action=\"/settings/update\" enctype=\"multipart/form-data\">"
       "<input type=\"hidden\" name=\"kind\" value=\"fs\">"
       "<label>Пароль обновления</label><input type=\"password\" name=\"pass\" autocomplete=\"off\" required>"
       "<label>Файл littlefs.bin</label><input type=\"file\" name=\"file\" accept=\".bin\" required>"
       "<button type=\"submit\">Обновить страницы</button></form></div>";

  h += "<div class=\"card\"><p class=\"hint\" style=\"margin:0\" id=\"otastatus\">Во время загрузки не закрывайте страницу и не выключайте устройство. "
       "Пароль задаётся в файле ota_update.h.</p>"
       "<progress id=\"otabar\" value=\"0\" max=\"100\" style=\"width:100%;display:none\"></progress></div>";

  // Загрузка через XMLHttpRequest - чтобы показывать процент и не оставлять пустую страницу
  h += "<script>"
       "document.querySelectorAll('form.otaform').forEach(function(f){f.addEventListener('submit',function(e){"
       "e.preventDefault();var st=document.getElementById('otastatus'),bar=document.getElementById('otabar');"
       "var x=new XMLHttpRequest();x.open('POST','/settings/update');bar.style.display='block';"
       "x.upload.onprogress=function(p){if(p.lengthComputable){var v=Math.round(p.loaded*100/p.total);bar.value=v;st.textContent='Загрузка: '+v+'%';}};"
       "x.upload.onload=function(){st.textContent='Файл передан, идёт проверка и запись...';};"
       "x.onload=function(){document.open();document.write(x.responseText);document.close();};"
       "x.onerror=function(){st.textContent='Связь прервана. Если устройство перезагрузилось - откройте страницу заново.';};"
       "x.send(new FormData(f));});});"
       "</script></div></body></html>";
  return h;
}

void otaRegisterWeb(AsyncWebServer &srv) {
  srv.on("/settings/update", HTTP_GET, [](AsyncWebServerRequest *req) {
    sendHtmlChunked(req, buildOtaPage());
  });
  srv.on("/settings/update", HTTP_POST, handleOtaDone, handleOtaUpload);
  srv.on("/update/url", HTTP_GET, handleOtaUrl);
}

// ------------------------------------------------------------
// OTA по ссылке: плата сама скачивает прошивку с сервера и пишет
// её кусками. Не требует multipart-загрузки из браузера.
// ------------------------------------------------------------
static void otaUrlTask(void *arg) {
  String url = *(String *)arg;
  delete (String *)arg;
  Serial.println(String("⬇️ OTA по ссылке: ") + url);

  WiFiClient client;
  client.setTimeout(15);
  HTTPClient http;
  http.setTimeout(15000);
  http.setReuse(false);
  if (!http.begin(client, url)) {
    Serial.println("❌ OTA: не удалось открыть URL");
    vTaskDelete(nullptr);
    return;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("❌ OTA: HTTP %d\n", code);
    http.end(); client.stop();
    vTaskDelete(nullptr);
    return;
  }
  int len = http.getSize();
  if (!Update.begin(len > 0 ? len : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    Serial.println(String("❌ OTA: Update.begin: ") + Update.errorString());
    http.end(); client.stop();
    vTaskDelete(nullptr);
    return;
  }
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[1024];
  size_t total = 0;
  while (http.connected() && (len <= 0 || total < (size_t)len)) {
    size_t n = stream->readBytes(buf, sizeof(buf));
    if (n) {
      if (Update.write(buf, n) != n) break;
      total += n;
    }
    vTaskDelay(1);
  }
  bool ok = Update.end(true);
  if (!ok) Update.printError(Serial);
  http.end();
  client.stop();
  Serial.printf("OTA по ссылке: %u байт, %s\n", (unsigned)total, ok ? "ok" : "fail");
  if (ok) {
    delay(500);
    ESP.restart();
  }
  vTaskDelete(nullptr);
}

static void handleOtaUrl(AsyncWebServerRequest *req) {
  if (!req->hasParam("url")) {
    req->send(400, "text/plain", "url required");
    return;
  }
  String *u = new String(req->getParam("url")->value());
  xTaskCreate(otaUrlTask, "otaurl", 8192, u, 1, nullptr);
  req->send(200, "text/html; charset=utf-8",
    "<!DOCTYPE html><html><head><meta charset='utf-8'></head><body>"
    "<h1>Обновление начато</h1><p>Плата скачивает прошивку сама. "
    "Через ~30 секунд откройте главную страницу.</p></body></html>");
}
