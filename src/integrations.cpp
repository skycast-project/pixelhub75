#include "integrations.h"
#include "web_send.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include "globals.h"
#include "sensors.h"
#include <HTTPClient.h>
#include <MD5Builder.h>
#include <ESPAsyncWebServer.h>
#include <math.h>

// Единицы измерения скорости ветра в данных погоды. Если у вас там км/ч - поменяйте на "km/h".
#define WIND_UNIT "m/s"

// Народмон просит не чаще раза в 5 минут
static const unsigned long NM_INTERVAL_MS = 300000UL;

// ------------------------------------------------------------
// Снимок данных (заполняется из основного потока, читается задачей)
// ------------------------------------------------------------
struct Snap {
  bool homeValid = false;
  bool homeHasHum = false;
  float homeTemp = 0, homeHum = 0, homePressHpa = 0;
  bool outValid = false;
  float outTemp = 0;
  bool wxValid = false;
  float wxTemp = 0, wxHum = 0, wxPress = 0, wxWind = 0;
  float lat = 0, lon = 0;
};

static IntegrationsConfig g_cfg;
static Snap g_snap;
static uint32_t g_cfgVersion = 1;
static SemaphoreHandle_t g_mux = nullptr;

// Состояние для веб-страницы (под мьютексом)
static bool g_mqttConnected = false;
static String g_mqttErr = "";
static unsigned long g_mqttLastPubMs = 0;   // 0 = ещё не отправляли
static String g_nmResult = "ожидание первой отправки";
static unsigned long g_nmLastMs = 0;

struct Lock {
  Lock() { xSemaphoreTake(g_mux, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_mux); }
};

static void ensureMux() {
  if (!g_mux) g_mux = xSemaphoreCreateMutex();
}

static String macId() {   // aabbccddeeff
  String m = WiFi.macAddress();
  m.replace(":", "");
  m.toLowerCase();
  return m;
}

String integrationsMac() {
  String m = macId();
  m.toUpperCase();
  return m;
}

// ------------------------------------------------------------
// ПРИЁМ: принятые значения (под мьютексом) и вспомогательные функции
// ------------------------------------------------------------
static String g_mqttRxVal[RX_ITEMS];
static unsigned long g_mqttRxMs[RX_ITEMS] = {0, 0, 0, 0};   // 0 = значения ещё нет
static String g_mqttRxText;
static unsigned long g_mqttRxTextMs = 0;
static String g_nmRxVal[RX_ITEMS];
static unsigned long g_nmRxMs[RX_ITEMS] = {0, 0, 0, 0};
static uint32_t g_rxVersion = 0;
static String g_nmRxResult = "ожидание первого запроса";
static unsigned long g_nmRxLastMs = 0;

static unsigned long nowMs() {
  unsigned long m = millis();
  return m ? m : 1;
}

static bool hexColorOk(const String &h) {
  if (h.length() != 7 || h[0] != '#') return false;
  for (int i = 1; i < 7; i++) if (!isxdigit((unsigned char)h[i])) return false;
  return true;
}

static uint32_t hexToRgb(const String &h) {
  if (!hexColorOk(h)) return 0xFFFFFF;
  return (uint32_t)strtoul(h.c_str() + 1, nullptr, 16);
}

static String digitsOnly(const String &s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) if (isdigit((unsigned char)s[i])) o += s[i];
  return o;
}

static String alnumOnly(const String &s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) if (isalnum((unsigned char)s[i])) o += s[i];
  return o;
}

// Предельная длина подписи и единицы - В БАЙТАХ. Кириллица в UTF-8 = 2 байта на букву,
// поэтому 64 байта = примерно 32 русские буквы. Хотите длиннее - увеличьте число.
#define RX_LABEL_MAX_BYTES 64
#define RX_UNIT_MAX_BYTES 16

// Обрезает строку до maxBytes байт, не разрезая многобайтовый символ UTF-8
// (раньше русское слово могло обрезаться посреди буквы - получались кракозябры)
static String utf8Truncate(const String &s, unsigned maxBytes) {
  if (s.length() <= maxBytes) return s;
  unsigned cut = maxBytes;
  // 10xxxxxx - продолжение символа: отступаем назад до начала символа
  while (cut > 0 && (((unsigned char)s[cut]) & 0xC0) == 0x80) cut--;
  return s.substring(0, cut);
}

static String cleanText(String s, unsigned maxLen) {
  s.trim();
  s.replace("\"", "");
  s.replace("<", "");
  s.replace(">", "");
  return utf8Truncate(s, maxLen);
}

// Приводит принятые настройки в допустимый вид
static void sanitizeRx(IntegrationsConfig &c) {
  for (int i = 0; i < RX_ITEMS; i++) {
    c.mqttRxTopic[i] = cleanText(c.mqttRxTopic[i], 120);
    c.mqttRxLabel[i] = cleanText(c.mqttRxLabel[i], RX_LABEL_MAX_BYTES);
    c.mqttRxUnit[i] = cleanText(c.mqttRxUnit[i], RX_UNIT_MAX_BYTES);
    c.nmRxId[i] = digitsOnly(c.nmRxId[i]);
    c.nmRxLabel[i] = cleanText(c.nmRxLabel[i], RX_LABEL_MAX_BYTES);
    c.nmRxUnit[i] = cleanText(c.nmRxUnit[i], RX_UNIT_MAX_BYTES);
  }
  c.mqttRxTextTopic = cleanText(c.mqttRxTextTopic, 120);
  c.nmApiKey = alnumOnly(c.nmApiKey);
  if (c.mqttRxStaleMin < 0) c.mqttRxStaleMin = 0;
  if (c.mqttRxStaleMin > 1440) c.mqttRxStaleMin = 1440;
  if (c.nmRxStaleMin < 0) c.nmRxStaleMin = 0;
  if (c.nmRxStaleMin > 1440) c.nmRxStaleMin = 1440;
  if (c.nmRxInterval < 5) c.nmRxInterval = 5;
  if (c.nmRxInterval > 1440) c.nmRxInterval = 1440;
  if (c.mqttRxSpeed < 1) c.mqttRxSpeed = 1;
  if (c.mqttRxSpeed > 10) c.mqttRxSpeed = 10;
  if (c.nmRxSpeed < 1) c.nmRxSpeed = 1;
  if (c.nmRxSpeed > 10) c.nmRxSpeed = 10;
  if (!hexColorOk(c.mqttRxColor)) c.mqttRxColor = "#40C0FF";
  if (!hexColorOk(c.nmRxColor)) c.nmRxColor = "#80FF80";
}

// Сбрасывает принятые значения (вызывать при смене настроек, под мьютексом)
static void rxClearLocked() {
  for (int i = 0; i < RX_ITEMS; i++) {
    g_mqttRxVal[i] = "";
    g_mqttRxMs[i] = 0;
    g_nmRxVal[i] = "";
    g_nmRxMs[i] = 0;
  }
  g_mqttRxText = "";
  g_mqttRxTextMs = 0;
  g_rxVersion++;
}

// Число "5.23" -> "5.2", "12.0" -> "12"; нечисловой текст оставляем как есть
static String rxFormat(String v, unsigned maxLen) {
  v.trim();
  if (v.length() == 0) return v;
  char *end = nullptr;
  double d = strtod(v.c_str(), &end);
  if (end != v.c_str() && *end == 0 && isfinite(d)) {
    char buf[24];
    if (fabs(d - round(d)) < 0.05) snprintf(buf, sizeof(buf), "%.0f", d);
    else snprintf(buf, sizeof(buf), "%.1f", d);
    return String(buf);
  }
  return utf8Truncate(v, maxLen);
}

// Входящее MQTT-сообщение (вызывается из g_mqtt.loop() в фоновой задаче, мьютекс в этот момент НЕ занят)
static void mqttOnMessage(char *topic, uint8_t *payload, unsigned int len) {
  if (len > 200) len = 200;
  String raw;
  raw.reserve(len);
  for (unsigned int i = 0; i < len; i++) raw += (char)payload[i];
  String t(topic);
  Lock l;
  bool changed = false;
  for (int i = 0; i < RX_ITEMS; i++) {
    if (g_cfg.mqttRxTopic[i].length() && t == g_cfg.mqttRxTopic[i]) {
      String v = rxFormat(raw, 40);
      if (g_mqttRxMs[i] == 0 || g_mqttRxVal[i] != v) changed = true;
      g_mqttRxVal[i] = v;
      g_mqttRxMs[i] = nowMs();
    }
  }
  if (g_cfg.mqttRxTextTopic.length() && t == g_cfg.mqttRxTextTopic) {
    String v = rxFormat(raw, 120);
    if (g_mqttRxTextMs == 0 || g_mqttRxText != v) changed = true;
    g_mqttRxText = v;
    g_mqttRxTextMs = nowMs();
  }
  if (changed) g_rxVersion++;
}

// ------------------------------------------------------------
// Хранение настроек (NVS)
// ------------------------------------------------------------
static void saveToNvs(const IntegrationsConfig &c) {
  Preferences p;
  if (!p.begin("integr", false)) return;
  p.putBool("mq_en", c.mqttEnabled);
  p.putInt("mq_mode", c.mqttMode);
  p.putString("mq_host", c.mqttHost);
  p.putInt("mq_port", c.mqttPort);
  p.putString("mq_user", c.mqttUser);
  p.putString("mq_pass", c.mqttPass);
  p.putString("mq_base", c.mqttBase);
  p.putInt("mq_int", c.mqttInterval);
  p.putBool("mq_home", c.mqttHome);
  p.putBool("mq_out", c.mqttOutdoor);
  p.putBool("mq_wx", c.mqttWeather);
  p.putBool("mq_st", c.mqttStatus);
  p.putBool("nm_en", c.nmEnabled);
  p.putString("nm_name", c.nmName);
  p.putBool("nm_home", c.nmHome);
  p.putBool("nm_out", c.nmOutdoor);
  // приём
  p.putBool("rx_en", c.mqttRxEnabled);
  for (int i = 0; i < RX_ITEMS; i++) {
    p.putString((String("rx_t") + i).c_str(), c.mqttRxTopic[i]);
    p.putString((String("rx_l") + i).c_str(), c.mqttRxLabel[i]);
    p.putString((String("rx_u") + i).c_str(), c.mqttRxUnit[i]);
    p.putString((String("nr_i") + i).c_str(), c.nmRxId[i]);
    p.putString((String("nr_l") + i).c_str(), c.nmRxLabel[i]);
    p.putString((String("nr_u") + i).c_str(), c.nmRxUnit[i]);
  }
  p.putString("rx_txt", c.mqttRxTextTopic);
  p.putInt("rx_stale", c.mqttRxStaleMin);
  p.putBool("rx_on", c.mqttRxLineOn);
  p.putString("rx_col", c.mqttRxColor);
  p.putInt("rx_spd", c.mqttRxSpeed);
  p.putBool("nr_en", c.nmRxEnabled);
  p.putString("nr_key", c.nmApiKey);
  p.putInt("nr_int", c.nmRxInterval);
  p.putInt("nr_stale", c.nmRxStaleMin);
  p.putBool("nr_on", c.nmRxLineOn);
  p.putString("nr_col", c.nmRxColor);
  p.putInt("nr_spd", c.nmRxSpeed);
  p.end();
}

void loadIntegrationsConfig() {
  ensureMux();
  IntegrationsConfig c;
  Preferences p;
  if (p.begin("integr", false)) {
    c.mqttEnabled = p.getBool("mq_en", c.mqttEnabled);
    c.mqttMode = p.getInt("mq_mode", c.mqttMode);
    c.mqttHost = p.getString("mq_host", c.mqttHost);
    c.mqttPort = p.getInt("mq_port", c.mqttPort);
    c.mqttUser = p.getString("mq_user", c.mqttUser);
    c.mqttPass = p.getString("mq_pass", c.mqttPass);
    c.mqttBase = p.getString("mq_base", c.mqttBase);
    c.mqttInterval = p.getInt("mq_int", c.mqttInterval);
    c.mqttHome = p.getBool("mq_home", c.mqttHome);
    c.mqttOutdoor = p.getBool("mq_out", c.mqttOutdoor);
    c.mqttWeather = p.getBool("mq_wx", c.mqttWeather);
    c.mqttStatus = p.getBool("mq_st", c.mqttStatus);
    c.nmEnabled = p.getBool("nm_en", c.nmEnabled);
    c.nmName = p.getString("nm_name", c.nmName);
    c.nmHome = p.getBool("nm_home", c.nmHome);
    c.nmOutdoor = p.getBool("nm_out", c.nmOutdoor);
    // приём
    c.mqttRxEnabled = p.getBool("rx_en", c.mqttRxEnabled);
    for (int i = 0; i < RX_ITEMS; i++) {
      c.mqttRxTopic[i] = p.getString((String("rx_t") + i).c_str(), "");
      c.mqttRxLabel[i] = p.getString((String("rx_l") + i).c_str(), "");
      c.mqttRxUnit[i] = p.getString((String("rx_u") + i).c_str(), "");
      c.nmRxId[i] = p.getString((String("nr_i") + i).c_str(), "");
      c.nmRxLabel[i] = p.getString((String("nr_l") + i).c_str(), "");
      c.nmRxUnit[i] = p.getString((String("nr_u") + i).c_str(), "");
    }
    c.mqttRxTextTopic = p.getString("rx_txt", c.mqttRxTextTopic);
    c.mqttRxStaleMin = p.getInt("rx_stale", c.mqttRxStaleMin);
    c.mqttRxLineOn = p.getBool("rx_on", c.mqttRxLineOn);
    c.mqttRxColor = p.getString("rx_col", c.mqttRxColor);
    c.mqttRxSpeed = p.getInt("rx_spd", c.mqttRxSpeed);
    c.nmRxEnabled = p.getBool("nr_en", c.nmRxEnabled);
    c.nmApiKey = p.getString("nr_key", c.nmApiKey);
    c.nmRxInterval = p.getInt("nr_int", c.nmRxInterval);
    c.nmRxStaleMin = p.getInt("nr_stale", c.nmRxStaleMin);
    c.nmRxLineOn = p.getBool("nr_on", c.nmRxLineOn);
    c.nmRxColor = p.getString("nr_col", c.nmRxColor);
    c.nmRxSpeed = p.getInt("nr_spd", c.nmRxSpeed);
    p.end();
  }
  if (c.mqttInterval < 10) c.mqttInterval = 10;
  if (c.mqttMode < 0 || c.mqttMode > 1) c.mqttMode = 0;
  sanitizeRx(c);
  Lock l;
  g_cfg = c;
}

IntegrationsConfig getIntegrationsConfig() {
  ensureMux();
  Lock l;
  return g_cfg;
}

void setIntegrationsConfig(const IntegrationsConfig &in) {
  ensureMux();
  IntegrationsConfig c = in;
  if (c.mqttInterval < 10) c.mqttInterval = 10;
  if (c.mqttPort < 1 || c.mqttPort > 65535) c.mqttPort = 1883;
  if (c.mqttMode < 0 || c.mqttMode > 1) c.mqttMode = 0;
  if (c.nmName.length() == 0) c.nmName = "PixelHub75";
  sanitizeRx(c);
  {
    Lock l;
    g_cfg = c;
    g_cfgVersion++;
    rxClearLocked();   // подписки/датчики могли измениться - старые значения не показываем
  }
  saveToNvs(c);
}

// ------------------------------------------------------------
// Снимок данных - вызывается из loop() раз в секунду
// ------------------------------------------------------------
void integrationsUpdateSnapshot() {
  ensureMux();
  Snap s;
  s.homeValid = (detectedSensor != SensorType::NONE);
  s.homeHasHum = sensorHasHumidity(detectedSensor);
  s.homeTemp = homeTemp;
  s.homeHum = homeHumidity;
  s.homePressHpa = homePressure;
  s.outValid = outdoorTempValid;
  s.outTemp = outdoorTemp;
  // Погода считается актуальной, если хотя бы раз успешно обновилась
  s.wxValid = (weatherSourceUsed.length() > 0);
  s.wxTemp = temperaturedarc;
  s.wxHum = (float)humiditydarc;
  s.wxPress = pressuredarc;
  s.wxWind = windSpeeddarc;
  s.lat = lat;
  s.lon = lon;
  Lock l;
  g_snap = s;
}

// ------------------------------------------------------------
// MQTT
// ------------------------------------------------------------
static WiFiClient g_wifi;
static PubSubClient g_mqtt(g_wifi);

static String mqttBaseOf(const IntegrationsConfig &c) {
  String b = c.mqttBase;
  b.trim();
  b.replace("\"", "");
  while (b.endsWith("/")) b.remove(b.length() - 1);
  if (b.length() == 0) b = "pixelhub";
  return b;
}

static void mqttSetState(bool connected, const String &err) {
  Lock l;
  g_mqttConnected = connected;
  g_mqttErr = err;
}

static String mqttErrText(int rc) {
  switch (rc) {
    case -4: return "тайм-аут ответа брокера";
    case -3: return "соединение потеряно";
    case -2: return "нет связи с брокером (проверьте адрес и порт)";
    case -1: return "отключён";
    case 1: return "брокер не принимает версию протокола";
    case 2: return "брокер отклонил идентификатор клиента";
    case 3: return "брокер недоступен";
    case 4: return "неверный логин или пароль";
    case 5: return "доступ запрещён (нет авторизации)";
    default: return "код " + String(rc);
  }
}

static void pubNum(const String &base, const char *sub, float v, int dec) {
  char val[24];
  snprintf(val, sizeof(val), "%.*f", dec, v);
  g_mqtt.publish((base + "/" + sub).c_str(), val);
}

static void pubStr(const String &base, const char *sub, const String &v) {
  g_mqtt.publish((base + "/" + sub).c_str(), v.c_str());
}

// Описание сущностей для Home Assistant
struct DiscItem {
  int group;            // 0 дом, 1 улица, 2 погода, 3 состояние
  const char *obj;
  const char *name;
  const char *sub;      // подтопик состояния
  const char *unit;
  const char *cls;      // device_class
};

static const DiscItem DISC[] = {
  {0, "home_temp", "Температура (дома)", "home/temp", "°C", "temperature"},
  {0, "home_hum", "Влажность (дома)", "home/humidity", "%", "humidity"},
  {0, "home_press", "Давление (дома)", "home/pressure", "hPa", "pressure"},
  {0, "home_press_mm", "Давление (дома), мм рт. ст.", "home/pressure_mmhg", "mmHg", "pressure"},
  {1, "out_temp", "Температура (улица)", "outdoor/temp", "°C", "temperature"},
  {2, "wx_temp", "Температура (погода)", "weather/temp", "°C", "temperature"},
  {2, "wx_hum", "Влажность (погода)", "weather/humidity", "%", "humidity"},
  {2, "wx_press", "Давление (погода)", "weather/pressure", "hPa", "pressure"},
  {2, "wx_wind", "Ветер (погода)", "weather/wind", WIND_UNIT, "wind_speed"},
  {3, "rssi", "Сигнал Wi-Fi", "status/rssi", "dBm", "signal_strength"},
  {3, "uptime", "Время работы", "status/uptime", "s", "duration"},
  {3, "heap", "Свободная память", "status/heap", "B", ""},
};

static void mqttPublishDiscovery(const IntegrationsConfig &c, const Snap &s, const String &base) {
  String mac = macId();
  for (const DiscItem &d : DISC) {
    bool en = (d.group == 0 && c.mqttHome) || (d.group == 1 && c.mqttOutdoor) ||
              (d.group == 2 && c.mqttWeather) || (d.group == 3 && c.mqttStatus);
    if (d.group == 0 && strcmp(d.obj, "home_hum") == 0 && !s.homeHasHum) en = false;

    String topic = "homeassistant/sensor/pixelhub_" + mac + "/" + d.obj + "/config";
    String payload = "";
    if (en) {
      payload.reserve(480);
      payload = "{\"name\":\"" + String(d.name) + "\",\"uniq_id\":\"ph_" + mac + "_" + d.obj +
                "\",\"stat_t\":\"" + base + "/" + d.sub + "\",\"avty_t\":\"" + base + "/availability\"";
      if (d.unit[0]) payload += ",\"unit_of_meas\":\"" + String(d.unit) + "\"";
      if (d.cls[0]) payload += ",\"dev_cla\":\"" + String(d.cls) + "\"";
      if (d.group < 3) payload += ",\"stat_cla\":\"measurement\"";
      payload += ",\"dev\":{\"ids\":[\"pixelhub_" + mac + "\"],\"name\":\"PixelHub 75\",\"mf\":\"DIY\",\"mdl\":\"ESP32 HUB75\"}}";
    }
    // Пустое сообщение с retain удаляет сущность отключённой группы в Home Assistant
    g_mqtt.publish(topic.c_str(), (const uint8_t *)payload.c_str(), payload.length(), true);
    g_mqtt.loop();
  }
}

static void mqttPublishAll(const IntegrationsConfig &c, const Snap &s, const String &base) {
  if (c.mqttHome && s.homeValid) {
    pubNum(base, "home/temp", s.homeTemp, 1);
    if (s.homeHasHum) pubNum(base, "home/humidity", s.homeHum, 1);
    pubNum(base, "home/pressure", s.homePressHpa, 1);
    pubNum(base, "home/pressure_mmhg", s.homePressHpa * 0.750062f, 1);
  }
  if (c.mqttOutdoor && s.outValid) {
    pubNum(base, "outdoor/temp", s.outTemp, 1);
  }
  if (c.mqttWeather && s.wxValid) {
    pubNum(base, "weather/temp", s.wxTemp, 1);
    pubNum(base, "weather/humidity", s.wxHum, 0);
    pubNum(base, "weather/pressure", s.wxPress, 1);
    pubNum(base, "weather/wind", s.wxWind, 1);
  }
  if (c.mqttStatus) {
    pubNum(base, "status/rssi", (float)WiFi.RSSI(), 0);
    pubNum(base, "status/uptime", (float)(millis() / 1000UL), 0);
    pubNum(base, "status/heap", (float)ESP.getFreeHeap(), 0);
    pubStr(base, "status/ip", WiFi.localIP().toString());
  }
}

static void mqttTick(const IntegrationsConfig &c, const Snap &s, uint32_t ver) {
  static bool inited = false;
  static uint32_t seenVer = 0xFFFFFFFFUL;
  static String host;
  static String availTopic;
  static unsigned long nextTry = 0, lastPub = 0, backoff = 5000;

  if (!inited) {
    g_mqtt.setBufferSize(768);
    g_mqtt.setSocketTimeout(5);
    g_mqtt.setCallback(mqttOnMessage);
    inited = true;
  }

  if (!c.mqttEnabled) {
    if (g_mqtt.connected()) {
      g_mqtt.publish(availTopic.c_str(), "offline", true);
      g_mqtt.disconnect();
    }
    mqttSetState(false, "");
    seenVer = 0xFFFFFFFFUL;   // при повторном включении всё переинициализируем
    return;
  }

  if (ver != seenVer) {       // настройки менялись - переподключаемся с новыми
    seenVer = ver;
    if (g_mqtt.connected()) {
      g_mqtt.publish(availTopic.c_str(), "offline", true);
      g_mqtt.disconnect();
    }
    host = c.mqttHost;
    host.trim();
    g_mqtt.setServer(host.c_str(), (uint16_t)c.mqttPort);
    nextTry = 0;
    backoff = 5000;
    lastPub = 0;
  }

  if (host.length() == 0) {
    mqttSetState(false, "не задан адрес брокера");
    return;
  }

  if (!g_mqtt.connected()) {
    if (nextTry != 0 && millis() < nextTry) return;

    String base = mqttBaseOf(c);
    availTopic = base + "/availability";
    String cid = "pixelhub-" + macId();
    bool ok;
    if (c.mqttUser.length()) {
      ok = g_mqtt.connect(cid.c_str(), c.mqttUser.c_str(), c.mqttPass.c_str(), availTopic.c_str(), 0, true, "offline");
    } else {
      ok = g_mqtt.connect(cid.c_str(), nullptr, nullptr, availTopic.c_str(), 0, true, "offline");
    }
    if (ok) {
      Serial.println("✅ MQTT подключён: " + host);
      g_mqtt.publish(availTopic.c_str(), "online", true);
      if (c.mqttMode == 1) mqttPublishDiscovery(c, s, base);
      if (c.mqttRxEnabled) {   // подписки на принимаемые топики
        for (int i = 0; i < RX_ITEMS; i++) {
          if (c.mqttRxTopic[i].length()) g_mqtt.subscribe(c.mqttRxTopic[i].c_str());
        }
        if (c.mqttRxTextTopic.length()) g_mqtt.subscribe(c.mqttRxTextTopic.c_str());
      }
      mqttSetState(true, "");
      lastPub = 0;
      backoff = 5000;
    } else {
      String err = "ошибка подключения: " + mqttErrText(g_mqtt.state());
      Serial.println("❌ MQTT " + err);
      mqttSetState(false, err);
      nextTry = millis() + backoff;
      if (nextTry == 0) nextTry = 1;
      backoff = min(backoff * 2, 120000UL);
      return;
    }
  }

  g_mqtt.loop();
  if (!g_mqtt.connected()) {
    mqttSetState(false, "связь потеряна, переподключаюсь");
    return;
  }

  unsigned long iv = (unsigned long)max(10, c.mqttInterval) * 1000UL;
  if (lastPub == 0 || millis() - lastPub >= iv) {
    mqttPublishAll(c, s, mqttBaseOf(c));
    lastPub = millis();
    if (lastPub == 0) lastPub = 1;
    Lock l;
    g_mqttLastPubMs = lastPub;
  }
}

String integrationsMqttStatus() {
  ensureMux();
  Lock l;
  if (!g_cfg.mqttEnabled) return "выключено";
  if (g_mqttConnected) {
    String t = "подключено";
    if (g_mqttLastPubMs != 0) t += ", последняя отправка " + String((millis() - g_mqttLastPubMs) / 1000UL) + " с назад";
    return t;
  }
  return g_mqttErr.length() ? g_mqttErr : String("подключение...");
}

// ------------------------------------------------------------
// Народный мониторинг (TCP, порт 8283)
// Формат: #MAC#NAME#LAT#LON \n #T1#значение \n ... ##
// T1 - температура дома, H1 - влажность, P1 - давление (мм рт. ст.),
// T2 - температура на улице. Датчики регистрируются на сайте автоматически.
// ------------------------------------------------------------
static void narodmonTick(const IntegrationsConfig &c, const Snap &s) {
  static unsigned long nextAt = 0;
  static bool first = true;
  if (first) {
    first = false;
    nextAt = millis() + 30000UL;   // первая отправка через 30 с после старта
  }
  if ((long)(millis() - nextAt) < 0) return;

  if (s.lat == 0 && s.lon == 0) {
    Lock l;
    g_nmResult = "не заданы координаты (страница «Погода»)";
    nextAt = millis() + 30000UL;
    return;
  }

  String name = c.nmName;
  name.replace("#", " ");
  name.trim();
  if (name.length() == 0) name = "PixelHub75";

  String b = "#" + integrationsMac() + "#" + name + "#" + String(s.lat, 6) + "#" + String(s.lon, 6) + "\n";
  int n = 0;
  if (c.nmHome && s.homeValid) {
    b += "#T1#" + String(s.homeTemp, 1) + "\n";
    n++;
    if (s.homeHasHum) {
      b += "#H1#" + String(s.homeHum, 0) + "\n";
      n++;
    }
    b += "#P1#" + String(s.homePressHpa * 0.750062f, 1) + "\n";
    n++;
  }
  if (c.nmOutdoor && s.outValid) {
    b += "#T2#" + String(s.outTemp, 1) + "\n";
    n++;
  }
  b += "##\n";

  if (n == 0) {   // нечего слать - не считаем это отправкой, проверим снова через 30 с
    Lock l;
    g_nmResult = "нет данных датчиков для отправки";
    nextAt = millis() + 30000UL;
    return;
  }

  String result;
  WiFiClient cl;
  if (!cl.connect("narodmon.ru", 8283, 5000)) {
    result = "не удалось подключиться к narodmon.ru";
  } else {
    cl.print(b);
    unsigned long t0 = millis();
    while (!cl.available() && cl.connected() && millis() - t0 < 3000) vTaskDelay(pdMS_TO_TICKS(50));
    String resp = cl.available() ? cl.readStringUntil('\n') : String("");
    resp.trim();
    cl.stop();
    if (resp.length() == 0) result = "отправлено (ответа сервера нет)";
    else if (resp.startsWith("OK")) result = "отправлено, сервер ответил OK";
    else result = "ответ сервера: " + resp;
  }
  Serial.println("Народмон: " + result);
  nextAt = millis() + NM_INTERVAL_MS;
  Lock l;
  g_nmResult = result;
  g_nmLastMs = millis();
  if (g_nmLastMs == 0) g_nmLastMs = 1;
}

String integrationsNarodmonStatus() {
  ensureMux();
  Lock l;
  if (!g_cfg.nmEnabled) return "выключено";
  String t = g_nmResult;
  if (g_nmLastMs != 0) t += " (" + String((millis() - g_nmLastMs) / 60000UL) + " мин назад)";
  return t;
}

// ------------------------------------------------------------
// ПРИЁМ с Народного мониторинга (HTTP API, команда sensorsValues)
// Нужен API-ключ приложения и ID датчиков. Запрос - не чаще раза в 5 минут.
// Если сервис не отвечает - смените адрес ниже (http://narodmon.ru/api).
// ------------------------------------------------------------
static const char *NM_API_URL = "http://api.narodmon.ru";

static String nmUuid() {   // постоянный MD5-идентификатор этой установки
  MD5Builder m;
  m.begin();
  m.add("pixelhub75-" + macId());
  m.calculate();
  return m.toString();
}

static void setNmRxResult(const String &s) {
  Lock l;
  g_nmRxResult = s;
  g_nmRxLastMs = nowMs();
}

// Ищет в ответе {"sensors":[{"id":123,"value":5.2,...}]} значение датчика id (без ArduinoJson)
static bool nmFindValue(const String &r, const String &id, String &out) {
  int pos = 0;
  int len = (int)r.length();
  while (true) {
    pos = r.indexOf("\"id\"", pos);
    if (pos < 0) return false;
    int c = r.indexOf(':', pos + 4);
    if (c < 0) return false;
    int i = c + 1;
    while (i < len && (r[i] == ' ' || r[i] == '"')) i++;
    String num;
    while (i < len && isdigit((unsigned char)r[i])) num += r[i++];
    pos += 4;
    if (num != id) continue;
    int s = r.lastIndexOf('{', pos);
    int e = r.indexOf('}', pos);
    if (s < 0 || e < 0) return false;
    int v = r.indexOf("\"value\"", s);
    if (v < 0 || v > e) return false;
    int vc = r.indexOf(':', v + 7);
    if (vc < 0 || vc > e) return false;
    int j = vc + 1;
    while (j < e && (r[j] == ' ' || r[j] == '"')) j++;
    String val;
    while (j < e && r[j] != ',' && r[j] != '"' && r[j] != '}') val += r[j++];
    val.trim();
    if (val.length() == 0 || val == "null") return false;
    out = val;
    return true;
  }
}

static void narodmonRxTick(const IntegrationsConfig &c, uint32_t ver) {
  static unsigned long nextAt = 0;
  static uint32_t seenVer = 0xFFFFFFFFUL;
  if (ver != seenVer) {          // настройки менялись - спросим через 5 с
    seenVer = ver;
    nextAt = millis() + 5000UL;
  }
  if (!c.nmRxEnabled) return;
  if ((long)(millis() - nextAt) < 0) return;

  String key = c.nmApiKey;
  String ids[RX_ITEMS];
  String idList;
  int n = 0;
  for (int i = 0; i < RX_ITEMS; i++) {
    ids[i] = digitsOnly(c.nmRxId[i]);
    if (ids[i].length()) {
      if (n) idList += ",";
      idList += ids[i];
      n++;
    }
  }
  if (key.length() == 0 || n == 0) {
    setNmRxResult("не заданы API-ключ или датчики");
    nextAt = millis() + 30000UL;
    return;
  }

  String body = "{\"cmd\":\"sensorsValues\",\"sensors\":[" + idList + "],\"uuid\":\"" + nmUuid() +
                "\",\"api_key\":\"" + key + "\",\"lang\":\"ru\"}";

  String result;
  bool ok = false;
  WiFiClient cl;
  HTTPClient http;
  if (!http.begin(cl, NM_API_URL)) {
    result = "не удалось начать запрос";
  } else {
    http.setTimeout(8000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("User-Agent", "PixelHub75");
    http.addHeader("Narodmon-Api-Key", key);
    int code = http.POST(body);
    if (code == 200) {
      String resp = http.getString();
      int found = 0;
      {
        Lock l;
        bool changed = false;
        for (int i = 0; i < RX_ITEMS; i++) {
          if (!ids[i].length()) continue;
          String val;
          if (nmFindValue(resp, ids[i], val)) {
            String fv = rxFormat(val, 20);
            if (g_nmRxMs[i] == 0 || g_nmRxVal[i] != fv) changed = true;
            g_nmRxVal[i] = fv;
            g_nmRxMs[i] = nowMs();
            found++;
          }
        }
        if (changed) g_rxVersion++;
      }
      if (found > 0) {
        ok = true;
        result = "получено датчиков: " + String(found) + " из " + String(n);
      } else {
        String head = resp.substring(0, 120);
        head.replace("\n", " ");
        result = "в ответе нет значений: " + head;
      }
    } else if (code > 0) {
      result = "ответ сервера HTTP " + String(code);
    } else {
      result = "нет соединения: " + http.errorToString(code);
    }
    http.end();
  }
  Serial.println("Народмон (приём): " + result);
  setNmRxResult(result);
  // успех - по расписанию; сбой - повтор через 2 минуты (чтобы не нарушать лимиты сервиса)
  nextAt = millis() + (ok ? (unsigned long)c.nmRxInterval * 60000UL : 120000UL);
}

// ------------------------------------------------------------
// ПРИЁМ: строки для бегущей строки и статусы
// ------------------------------------------------------------
static void appendPart(String &out, const String &label, const String &val, const String &unit) {
  if (out.length()) out += ", ";
  if (label.length()) out += label + " ";
  out += val + unit;
}

String integrationsRxLineText(int which) {
  ensureMux();
  Lock l;
  String out;
  unsigned long now = millis();
  if (which == 0) {
    if (!g_cfg.mqttEnabled || !g_cfg.mqttRxEnabled || !g_cfg.mqttRxLineOn) return "";
    unsigned long stale = (unsigned long)g_cfg.mqttRxStaleMin * 60000UL;
    for (int i = 0; i < RX_ITEMS; i++) {
      if (!g_cfg.mqttRxTopic[i].length() || g_mqttRxMs[i] == 0) continue;
      if (stale && now - g_mqttRxMs[i] > stale) continue;
      appendPart(out, g_cfg.mqttRxLabel[i], g_mqttRxVal[i], g_cfg.mqttRxUnit[i]);
    }
    if (g_cfg.mqttRxTextTopic.length() && g_mqttRxTextMs != 0 && g_mqttRxText.length() &&
        !(stale && now - g_mqttRxTextMs > stale)) {
      if (out.length()) out += ".  ";
      out += g_mqttRxText;
    }
  } else {
    if (!g_cfg.nmRxEnabled || !g_cfg.nmRxLineOn) return "";
    unsigned long stale = (unsigned long)g_cfg.nmRxStaleMin * 60000UL;
    for (int i = 0; i < RX_ITEMS; i++) {
      if (!g_cfg.nmRxId[i].length() || g_nmRxMs[i] == 0) continue;
      if (stale && now - g_nmRxMs[i] > stale) continue;
      appendPart(out, g_cfg.nmRxLabel[i], g_nmRxVal[i], g_cfg.nmRxUnit[i]);
    }
  }
  if (out.length()) out += "   ";
  return out;
}

uint32_t integrationsRxLineColor(int which) {
  ensureMux();
  Lock l;
  return hexToRgb(which == 0 ? g_cfg.mqttRxColor : g_cfg.nmRxColor);
}

int integrationsRxLineSpeed(int which) {
  ensureMux();
  Lock l;
  int v = (which == 0) ? g_cfg.mqttRxSpeed : g_cfg.nmRxSpeed;
  return (v < 1) ? 5 : (v > 10 ? 10 : v);
}

uint32_t integrationsRxVersion() {
  ensureMux();
  Lock l;
  return g_rxVersion;
}

String integrationsMqttRxStatus() {
  ensureMux();
  Lock l;
  if (!g_cfg.mqttRxEnabled) return "выключено";
  if (!g_cfg.mqttEnabled) return "сначала включите MQTT на странице «Интеграции»";
  if (!g_mqttConnected) return "нет подключения к брокеру";
  int got = 0;
  for (int i = 0; i < RX_ITEMS; i++) if (g_mqttRxMs[i]) got++;
  if (g_mqttRxTextMs) got++;
  return "подписка активна, принято значений: " + String(got);
}

String integrationsNmRxStatus() {
  ensureMux();
  Lock l;
  if (!g_cfg.nmRxEnabled) return "выключено";
  String t = g_nmRxResult;
  if (g_nmRxLastMs != 0) t += " (" + String((millis() - g_nmRxLastMs) / 60000UL) + " мин назад)";
  return t;
}

// ------------------------------------------------------------
// ПРИЁМ: веб-страница /settings/receive (целиком здесь, без LittleFS и web_server.cpp)
// ------------------------------------------------------------
static String esc(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (unsigned i = 0; i < s.length(); i++) {
    char ch = s[i];
    if (ch == '&') o += "&amp;";
    else if (ch == '<') o += "&lt;";
    else if (ch == '>') o += "&gt;";
    else if (ch == '"') o += "&quot;";
    else o += ch;
  }
  return o;
}

static String chkAttr(bool b) { return b ? " checked" : ""; }

static String textInput(const String &name, const String &value, const char *ph = "") {
  return "<input type=\"text\" name=\"" + name + "\" value=\"" + esc(value) + "\" placeholder=\"" + ph + "\">";
}

static String lineSettings(const char *pfx, bool on, const String &color, int speed, int stale) {
  String p(pfx);
  String h;
  h += "<div class=\"checkbox-row\"><input type=\"checkbox\" id=\"" + p + "On\" name=\"" + p + "On\"" + chkAttr(on) +
       "><label for=\"" + p + "On\" style=\"margin:0\">Показывать в бегущей строке</label></div>";
  h += "<label>Цвет текста</label><input type=\"color\" name=\"" + p + "Color\" value=\"" + esc(color) + "\">";
  h += "<label>Скорость (1 — медленно, 10 — быстро)</label><input type=\"range\" name=\"" + p + "Speed\" min=\"1\" max=\"10\" value=\"" + String(speed) + "\">";
  h += "<label>Скрывать значение, если нет новых данных дольше (минут, 0 — не скрывать)</label><input type=\"number\" name=\"" + p + "Stale\" min=\"0\" max=\"1440\" value=\"" + String(stale) + "\">";
  return h;
}

static String buildReceivePage() {
  IntegrationsConfig c = getIntegrationsConfig();
  String h;
  h.reserve(7200);
  h += "<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
       "<title>Приём данных — PixelHub 75</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body><div class=\"wrap\">"
       "<div class=\"nav\"><a href=\"/\">Главная</a><a href=\"/settings/clock\">Часы</a>"
       "<a href=\"/settings/integrations\">Интеграции</a><a href=\"/settings/wifi\">Wi-Fi</a></div>"
       "<h1>Приём данных</h1>"
       "<p class=\"hint\">Значения идут в бегущую строку (MQTT и Народмон).</p>"
       "<form method=\"POST\" action=\"/settings/receive\">";

  // ---- MQTT ----
  h += "<div class=\"card\"><div class=\"line-title\">Приём по MQTT</div><p class=\"hint\">Состояние: <b>" + esc(integrationsMqttRxStatus()) + "</b></p>";
  h += "<div class=\"checkbox-row\"><input type=\"checkbox\" id=\"mqttRxEnabled\" name=\"mqttRxEnabled\"" + chkAttr(c.mqttRxEnabled) +
       "><label for=\"mqttRxEnabled\" style=\"margin:0\">Принимать данные по MQTT</label></div>";
  h += "<p class=\"hint\">Работает через то же подключение к брокеру: MQTT должен быть включён на странице «Интеграции». "
       "Топик пишется полностью, как его публикует источник (например home/garage/temp). Значение может быть числом или текстом.</p>";
  for (int i = 0; i < RX_ITEMS; i++) {
    String n = String(i);
    h += "<div class=\"line-block\"><div class=\"line-title\">Значение " + String(i + 1) + "</div>";
    h += "<label>Топик</label>" + textInput("mt" + n, c.mqttRxTopic[i], "home/garage/temp");
    h += "<label>Подпись</label>" + textInput("ml" + n, c.mqttRxLabel[i], "Гараж");
    h += "<label>Единица (C, %, мм …)</label>" + textInput("mu" + n, c.mqttRxUnit[i], "C");
    h += "</div>";
  }
  h += "<div class=\"line-block\"><div class=\"line-title\">Готовый текст</div>"
       "<p class=\"hint\" style=\"margin-top:0\">Топик, в который приходит фраза целиком — она показывается как есть (например уведомления из умного дома).</p>";
  h += "<label>Топик с текстом</label>" + textInput("mtext", c.mqttRxTextTopic, "home/notify");
  h += "</div>";
  h += lineSettings("mqttRx", c.mqttRxLineOn, c.mqttRxColor, c.mqttRxSpeed, c.mqttRxStaleMin);
  h += "</div>";

  // ---- Народмон ----
  h += "<div class=\"card\"><div class=\"line-title\">Приём с Народного мониторинга</div><p class=\"hint\">Состояние: <b>" +
       esc(integrationsNmRxStatus()) + "</b></p>";
  h += "<div class=\"checkbox-row\"><input type=\"checkbox\" id=\"nmRxEnabled\" name=\"nmRxEnabled\"" + chkAttr(c.nmRxEnabled) +
       "><label for=\"nmRxEnabled\" style=\"margin:0\">Читать датчики с narodmon.ru</label></div>";
  h += "<label>API-ключ приложения</label>" + textInput("nmApiKey", c.nmApiKey, "ключ из профиля narodmon.ru");
  h += "<p class=\"hint\">Ключ выдаётся на сайте после регистрации своего приложения (профиль → «Мои приложения»). "
       "ID датчика — число из адреса страницы датчика (narodmon.ru/S12345 → 12345). Идентификатор установки (uuid) формируется сам: " +
       esc(nmUuid()) + "</p>";
  for (int i = 0; i < RX_ITEMS; i++) {
    String n = String(i);
    h += "<div class=\"line-block\"><div class=\"line-title\">Датчик " + String(i + 1) + "</div>";
    h += "<label>ID датчика</label>" + textInput("ni" + n, c.nmRxId[i], "12345");
    h += "<label>Подпись</label>" + textInput("nl" + n, c.nmRxLabel[i], "Сосед");
    h += "<label>Единица</label>" + textInput("nu" + n, c.nmRxUnit[i], "C");
    h += "</div>";
  }
  h += "<label>Интервал запросов (минут, не меньше 5)</label><input type=\"number\" name=\"nmRxInterval\" min=\"5\" max=\"1440\" value=\"" + String(c.nmRxInterval) + "\">";
  h += lineSettings("nmRx", c.nmRxLineOn, c.nmRxColor, c.nmRxSpeed, c.nmRxStaleMin);
  h += "</div>";

  h += "<button type=\"submit\">Сохранить</button></form></div></body></html>";
  return h;
}

void integrationsRegisterWeb(AsyncWebServer &srv) {
  srv.on("/settings/receive", HTTP_GET, [](AsyncWebServerRequest *req) {
    sendHtmlChunked(req, buildReceivePage());
  });
  srv.on("/settings/receive", HTTP_POST, [](AsyncWebServerRequest *req) {
    auto P = [&](const String &name, const String &def) -> String {
      return req->hasParam(name, true) ? req->getParam(name, true)->value() : def;
    };
    IntegrationsConfig c = getIntegrationsConfig();   // настройки отправки остаются как были
    c.mqttRxEnabled = req->hasParam("mqttRxEnabled", true);
    c.nmRxEnabled = req->hasParam("nmRxEnabled", true);
    for (int i = 0; i < RX_ITEMS; i++) {
      String n = String(i);
      c.mqttRxTopic[i] = P("mt" + n, "");
      c.mqttRxLabel[i] = P("ml" + n, "");
      c.mqttRxUnit[i] = P("mu" + n, "");
      c.nmRxId[i] = P("ni" + n, "");
      c.nmRxLabel[i] = P("nl" + n, "");
      c.nmRxUnit[i] = P("nu" + n, "");
    }
    c.mqttRxTextTopic = P("mtext", "");
    c.nmApiKey = P("nmApiKey", "");
    c.nmRxInterval = P("nmRxInterval", String(c.nmRxInterval)).toInt();
    c.mqttRxLineOn = req->hasParam("mqttRxOn", true);
    c.mqttRxColor = P("mqttRxColor", c.mqttRxColor);
    c.mqttRxSpeed = P("mqttRxSpeed", String(c.mqttRxSpeed)).toInt();
    c.mqttRxStaleMin = P("mqttRxStale", String(c.mqttRxStaleMin)).toInt();
    c.nmRxLineOn = req->hasParam("nmRxOn", true);
    c.nmRxColor = P("nmRxColor", c.nmRxColor);
    c.nmRxSpeed = P("nmRxSpeed", String(c.nmRxSpeed)).toInt();
    c.nmRxStaleMin = P("nmRxStale", String(c.nmRxStaleMin)).toInt();
    setIntegrationsConfig(c);
    req->redirect("/settings/receive");
  });
}

// ------------------------------------------------------------
// Фоновая задача
// ------------------------------------------------------------
static void integrationsTask(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (WiFi.status() != WL_CONNECTED) continue;

    IntegrationsConfig c;
    Snap s;
    uint32_t ver;
    {
      Lock l;
      c = g_cfg;
      s = g_snap;
      ver = g_cfgVersion;
    }
    mqttTick(c, s, ver);
    if (c.nmEnabled) narodmonTick(c, s);
    narodmonRxTick(c, ver);
  }
}

void startIntegrationsTask() {
  ensureMux();
  xTaskCreatePinnedToCore(integrationsTask, "integr", 12288, nullptr, 1, nullptr, 0);
  Serial.println("✅ Задача интеграций (MQTT/Народмон) запущена");
}
