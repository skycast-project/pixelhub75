#include "wifi_setup.h"
#include "web_send.h"
#include <WiFi.h>
#include <DNSServer.h>
#include "web_server.h"   // объект server объявлен там (extern AsyncWebServer server;)
#include "globals.h"
#include "storage.h"
#include "display_manager.h"

static const char* AP_SSID = "PixelHub75-Setup";
static const byte DNS_PORT = 53;
static DNSServer dnsServer;
static bool apActive = false;

static void handleSetupPortal(AsyncWebServerRequest *request) {
  String html =
    "<!DOCTYPE html><html lang='ru'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>PixelHub 75 - Настройка Wi-Fi</title>"
    "<style>body{font-family:sans-serif;background:#111;color:#eee;display:flex;"
    "justify-content:center;padding-top:40px;margin:0}"
    ".card{background:#222;padding:24px;border-radius:12px;max-width:320px;width:90%}"
    "input{width:100%;padding:10px;margin:8px 0;border-radius:6px;border:none;box-sizing:border-box}"
    "button{width:100%;padding:12px;border:none;border-radius:6px;background:#0ac;color:#fff;font-size:16px}"
    "</style></head><body><div class='card'>"
    "<h2>📶 Настройка Wi-Fi</h2>"
    "<p>Введите данные вашей домашней сети:</p>"
    "<form method='POST' action='/save'>"
    "<input name='ssid' placeholder='Имя сети (SSID)' required>"
    "<input name='password' type='password' placeholder='Пароль'>"
    "<button type='submit'>Сохранить и перезагрузить</button>"
    "</form></div></body></html>";
  sendHtmlChunked(request, html);
}

static void handleSetupSave(AsyncWebServerRequest *request) {
  if (request->hasParam("ssid", true)) wifi_ssid = request->getParam("ssid", true)->value();
  if (request->hasParam("password", true)) wifi_password = request->getParam("password", true)->value();
  saveConfig();
  String html =
    "<!DOCTYPE html><html lang='ru'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'></head><body>"
    "<h2>Сохранено!</h2><p>Устройство перезагружается и подключается к вашей сети...</p>"
    "</body></html>";
  sendHtmlChunked(request, html);
  delay(1000);
  ESP.restart();
}

bool connectWiFiOrStartAP() {
  Serial.print("Подключение к Wi-Fi: ");
  Serial.println(wifi_ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 100) {
    updateWifiConnectingAnimation();
    delay(100);
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println("⚠️ Не удалось подключиться - поднимаю точку доступа");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("AP IP: ");
  Serial.println(apIP);

  dnsServer.start(DNS_PORT, "*", apIP); // ловит ВСЕ DNS-запросы на себя - телефон сам предложит "Войти в сеть"

  server.on("/", HTTP_GET, handleSetupPortal);
  server.on("/save", HTTP_POST, handleSetupSave);
  server.onNotFound(handleSetupPortal); // любой левый путь (проверки captive portal от iOS/Android) - та же страница
  server.begin();

  apActive = true;
  showApModeScreen(AP_SSID);
  return false;
}

void handleWifiSetupLoop() {
  if (apActive) dnsServer.processNextRequest();
}

bool isWifiSetupApActive() { return apActive; }
