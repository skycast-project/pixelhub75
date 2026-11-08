#pragma once
// ============================================================
// ИНТЕГРАЦИИ: MQTT (простой брокер / Home Assistant) и
// Народный мониторинг (narodmon.ru).
// Всё работает в отдельной FreeRTOS-задаче - loop() не блокируется,
// даже если брокер или сервер недоступны.
// Настройки хранятся в NVS (Preferences, пространство "integr"),
// поэтому НЕ стираются заливкой LittleFS и не требуют правок storage.cpp.
// ============================================================
#include <Arduino.h>

class AsyncWebServer;   // полностью объявлен в ESPAsyncWebServer.h

// Сколько значений можно принимать от каждого источника (MQTT / Народмон)
#define RX_ITEMS 4

struct IntegrationsConfig {
  // --- MQTT ---
  bool mqttEnabled = false;
  int mqttMode = 0;            // 0 = простой брокер (свои топики), 1 = Home Assistant (автообнаружение)
  String mqttHost = "";
  int mqttPort = 1883;
  String mqttUser = "";
  String mqttPass = "";
  String mqttBase = "pixelhub"; // корневой топик
  int mqttInterval = 60;        // секунд между отправками (мин. 10)
  bool mqttHome = true;         // домашний датчик
  bool mqttOutdoor = true;      // уличный DS18B20
  bool mqttWeather = true;      // текущая погода из интернета
  bool mqttStatus = true;       // состояние устройства (RSSI, аптайм, память, IP)
  // --- Народный мониторинг ---
  bool nmEnabled = false;
  String nmName = "PixelHub75"; // имя устройства на narodmon.ru
  bool nmHome = true;
  bool nmOutdoor = true;

  // --- ПРИЁМ по MQTT (подписка на чужие топики, вывод в бегущую строку) ---
  // Работает поверх подключения к брокеру: MQTT должен быть включён выше.
  bool mqttRxEnabled = false;
  String mqttRxTopic[RX_ITEMS];   // полный топик, например home/garage/temp
  String mqttRxLabel[RX_ITEMS];   // подпись в строке, например "Гараж"
  String mqttRxUnit[RX_ITEMS];    // единица, например "C" или "%"
  String mqttRxTextTopic = "";    // топик с готовым текстом (показывается как есть)
  int mqttRxStaleMin = 30;        // через сколько минут без новых данных значение пропадает (0 = не скрывать)
  bool mqttRxLineOn = true;       // показывать строку "MQTT" в бегущей строке
  String mqttRxColor = "#40C0FF";
  int mqttRxSpeed = 5;            // 1..10

  // --- ПРИЁМ с Народного мониторинга (чтение чужих/своих датчиков по API) ---
  bool nmRxEnabled = false;
  String nmApiKey = "";           // ключ приложения (narodmon.ru, профиль -> мои приложения)
  String nmRxId[RX_ITEMS];        // ID датчика (число)
  String nmRxLabel[RX_ITEMS];
  String nmRxUnit[RX_ITEMS];
  int nmRxInterval = 10;          // минут между запросами (не меньше 5)
  int nmRxStaleMin = 60;          // через сколько минут без успешного запроса значения пропадают (0 = не скрывать)
  bool nmRxLineOn = true;
  String nmRxColor = "#80FF80";
  int nmRxSpeed = 5;
};

// Вызвать в setup() сразу после loadConfig()
void loadIntegrationsConfig();
// Вызвать в setup() после startWeatherTask() (т.е. когда Wi-Fi уже есть)
void startIntegrationsTask();
// Вызывать раз в секунду из loop() (в блоке "if (millis() - lastTimeUpdate >= 1000)")
void integrationsUpdateSnapshot();

// Для веб-интерфейса
IntegrationsConfig getIntegrationsConfig();
void setIntegrationsConfig(const IntegrationsConfig &c);   // применяет и сохраняет в NVS
String integrationsMqttStatus();
String integrationsNarodmonStatus();
String integrationsMac();   // MAC без двоеточий, заглавными - для регистрации на narodmon.ru

// ---------- ПРИЁМ: строки для бегущей строки ----------
// which: 0 = MQTT, 1 = Народный мониторинг.
// Текст пустой, если строка выключена или нет свежих данных - drawScrollText() такие пропускает.
String integrationsRxLineText(int which);
uint32_t integrationsRxLineColor(int which);   // 0xRRGGBB
int integrationsRxLineSpeed(int which);        // 1..10
// Счётчик меняется, когда принятые данные реально изменились (по нему main.cpp пересобирает строки)
uint32_t integrationsRxVersion();
String integrationsMqttRxStatus();
String integrationsNmRxStatus();
// Регистрирует страницу /settings/receive (вызвать ДО setupWebServer())
void integrationsRegisterWeb(AsyncWebServer &srv);
