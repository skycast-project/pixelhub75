#include "local_sensors.h"
#include "web_send.h"
#include "web_send.h"
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "globals.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ------------------------------------------------------------
// Настройки, которые удобно менять
// ------------------------------------------------------------
static const char *LS_NVS_NS = "lsens";
static const int LS_ID_MAX = 16;            // длина id датчика (латиница, цифры, - и _)
static const int LS_NAME_MAX_BYTES = 16;    // длина названия на экране в байтах (кириллица = 2 байта на букву)
static const int LS_UNKNOWN_MAX = 4;        // сколько «неизвестных» датчиков помнить (чтобы подсказать на странице)
static const int LS_DEFAULT_STALE_MIN = 60; // через сколько минут без данных значение считается устаревшим

// ------------------------------------------------------------
// Состояние
// ------------------------------------------------------------
struct LsSlot {
  String id;               // "" - слот свободен
  String name;             // подпись на экране
  bool screen = true;      // выводить на экран «Комнаты»
  bool outdoor = false;    // использовать как уличный датчик
  uint8_t second = 0;      // вторая строка на экране: 0 авто, 1 влажность, 2 давление
  float offT = 0, offH = 0, offP = 0;   // поправки: °C, %, мм рт. ст.
};

struct LsConfig {
  bool screenOn = true;
  int8_t homeSrc = 0;      // 0 = встроенный I2C, 1..LS_SLOTS = Wi-Fi датчик для экрана «Дома»
  bool demo = false;       // демо: показывать экран «Комнаты» с тестовыми значениями, пока нет настоящих датчиков
  int staleMin = LS_DEFAULT_STALE_MIN;
  String key;              // пароль для запросов (пусто - не проверять)
  float lowBatt = 0;       // порог батарейки, В (0 - не предупреждать)
  LsSlot slots[LS_SLOTS];
};

struct LsVal {
  bool hasT = false, hasH = false, hasP = false, hasB = false;
  float t = 0, h = 0, p = 0, b = 0;   // p - гПа
  unsigned long ms = 0;               // 0 - данных ещё нет
};

struct LsUnknown {
  String id;
  float t = 0;
  bool hasT = false;
  unsigned long ms = 0;
};

static LsConfig g_cfg;
static LsVal g_vals[LS_SLOTS];
static LsUnknown g_unk[LS_UNKNOWN_MAX];
static SemaphoreHandle_t g_mux = nullptr;

struct LsLock {
  LsLock() { if (g_mux) xSemaphoreTake(g_mux, portMAX_DELAY); }
  ~LsLock() { if (g_mux) xSemaphoreGive(g_mux); }
};

static unsigned long nowMs() {
  unsigned long m = millis();
  return m ? m : 1;
}

// ------------------------------------------------------------
// Очистка введённого
// ------------------------------------------------------------
String localSensorsCleanId(const String &in) {
  String o;
  for (unsigned i = 0; i < in.length() && (int)o.length() < LS_ID_MAX; i++) {
    char c = in[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') o += c;
  }
  return o;
}

static String utf8Cut(const String &s, unsigned maxBytes) {
  if (s.length() <= maxBytes) return s;
  unsigned cut = maxBytes;
  while (cut > 0 && (((unsigned char)s[cut]) & 0xC0) == 0x80) cut--;
  return String(s.c_str(), cut);
}

// Название: латиница, цифры, знаки и кириллица (D0/D1 + байт)
static String cleanName(const String &in) {
  String o;
  unsigned n = in.length();
  for (unsigned i = 0; i < n; i++) {
    unsigned char c = (unsigned char)in[i];
    if (c >= 0x20 && c <= 0x7E) {
      if (c == '<' || c == '>' || c == '"' || c == '&') continue;
      o += (char)c;
    } else if ((c == 0xD0 || c == 0xD1) && i + 1 < n) {
      o += (char)c;
      o += in[++i];
    }
  }
  while (o.length() && o[0] == ' ') o = String(o.c_str() + 1);
  while (o.length() && o[o.length() - 1] == ' ') o = String(o.c_str(), o.length() - 1);
  return utf8Cut(o, LS_NAME_MAX_BYTES);
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ------------------------------------------------------------
// Хранение (NVS, один JSON)
// ------------------------------------------------------------
static String cfgToJson(const LsConfig &c) {
  JsonDocument doc;
  doc["on"] = c.screenOn ? 1 : 0;
  doc["dm"] = c.demo ? 1 : 0;
  doc["st"] = c.staleMin;
  doc["k"] = c.key.c_str();
  doc["lb"] = c.lowBatt;
  doc["hs"] = c.homeSrc;
  JsonArray arr = doc["s"].to<JsonArray>();
  for (int i = 0; i < LS_SLOTS; i++) {
    const LsSlot &s = c.slots[i];
    JsonObject o = arr.add<JsonObject>();
    o["i"] = s.id.c_str();
    o["n"] = s.name.c_str();
    o["e"] = s.screen ? 1 : 0;
    o["o"] = s.outdoor ? 1 : 0;
    o["w"] = s.second;
    o["t"] = s.offT;
    o["h"] = s.offH;
    o["p"] = s.offP;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

static bool cfgFromJson(const String &json, LsConfig &out) {
  JsonDocument doc;
  if (deserializeJson(doc, json.c_str())) return false;
  LsConfig c;
  c.screenOn = (doc["on"] | 1) != 0;
  c.demo = (doc["dm"] | 0) != 0;
  c.staleMin = constrain((int)(doc["st"] | LS_DEFAULT_STALE_MIN), 1, 1440);
  c.key = String((const char *)(doc["k"] | ""));
  c.lowBatt = clampf(doc["lb"] | 0.0f, 0.0f, 6.0f);
  c.homeSrc = (int8_t)constrain((int)(doc["hs"] | 0), 0, LS_SLOTS);
  int i = 0;
  bool outdoorTaken = false;
  for (JsonObject o : doc["s"].as<JsonArray>()) {
    if (i >= LS_SLOTS) break;
    LsSlot &s = c.slots[i++];
    s.id = localSensorsCleanId(String((const char *)(o["i"] | "")));
    s.name = cleanName(String((const char *)(o["n"] | "")));
    s.screen = (o["e"] | 1) != 0;
    s.outdoor = (o["o"] | 0) != 0 && !outdoorTaken && s.id.length();
    if (s.outdoor) outdoorTaken = true;
    s.second = (uint8_t)constrain((int)(o["w"] | 0), 0, 2);
    s.offT = clampf(o["t"] | 0.0f, -20.0f, 20.0f);
    s.offH = clampf(o["h"] | 0.0f, -30.0f, 30.0f);
    s.offP = clampf(o["p"] | 0.0f, -50.0f, 50.0f);
  }
  out = c;
  return true;
}

static void saveLocked() {
  String json = cfgToJson(g_cfg);
  Preferences p;
  if (!p.begin(LS_NVS_NS, false)) return;
  p.putString("cfg", json);
  p.end();
}

void localSensorsInit() {
  if (!g_mux) g_mux = xSemaphoreCreateMutex();
  LsLock l;
  g_cfg = LsConfig();
  for (int i = 0; i < LS_SLOTS; i++) g_vals[i] = LsVal();
  for (int i = 0; i < LS_UNKNOWN_MAX; i++) g_unk[i] = LsUnknown();
  Preferences p;
  if (p.begin(LS_NVS_NS, true)) {
    String json = p.getString("cfg", "");
    p.end();
    if (json.length() && cfgFromJson(json, g_cfg)) {
      int n = 0;
      for (int i = 0; i < LS_SLOTS; i++) if (g_cfg.slots[i].id.length()) n++;
      Serial.println("🌡️ Локальные датчики: настроено " + String(n));
      return;
    }
  }
  Serial.println("🌡️ Локальные датчики: пока не настроены");
}

// ------------------------------------------------------------
// Приём значений
// ------------------------------------------------------------
static bool isFresh(const LsVal &v, int staleMin) {
  if (v.ms == 0) return false;
  return (unsigned long)(millis() - v.ms) <= (unsigned long)staleMin * 60000UL;
}

// Возвращает код ответа: 200 принято, 202 принято от неизвестного датчика,
// 400 нет id, 403 неверный пароль
static int receiveValues(const String &idRaw, const String &key, const String *t, const String *h,
                         const String *p, const String *b, String &answer) {
  String id = localSensorsCleanId(idRaw);
  if (id.length() == 0) { answer = "no id"; return 400; }
  LsLock l;
  if (g_cfg.key.length() && key != g_cfg.key) { answer = "bad key"; return 403; }

  bool hasT = false, hasH = false, hasP = false, hasB = false;
  float vt = 0, vh = 0, vp = 0, vb = 0;
  if (t && t->length()) { vt = t->toFloat(); hasT = vt > -60.0f && vt < 85.0f; }
  if (h && h->length()) { vh = h->toFloat(); hasH = vh >= 0.0f && vh <= 100.0f; }
  if (p && p->length()) { vp = p->toFloat(); hasP = vp > 300.0f && vp < 1100.0f; }
  if (b && b->length()) { vb = b->toFloat(); hasB = vb > 0.0f && vb < 6.0f; }

  for (int i = 0; i < LS_SLOTS; i++) {
    if (g_cfg.slots[i].id != id) continue;
    LsVal &v = g_vals[i];
    if (hasT) { v.hasT = true; v.t = vt; }
    if (hasH) { v.hasH = true; v.h = vh; }
    if (hasP) { v.hasP = true; v.p = vp; }
    if (hasB) { v.hasB = true; v.b = vb; }
    if (hasT || hasH || hasP) v.ms = nowMs();
    answer = "OK";
    return 200;
  }
  // неизвестный датчик: запомним, чтобы страница подсказала, что его можно добавить
  int slot = -1;
  for (int i = 0; i < LS_UNKNOWN_MAX; i++) if (g_unk[i].id == id) { slot = i; break; }
  if (slot < 0) {
    unsigned long oldest = 0xFFFFFFFFUL;
    for (int i = 0; i < LS_UNKNOWN_MAX; i++) {
      if (g_unk[i].id.length() == 0) { slot = i; break; }
      if (g_unk[i].ms < oldest) { oldest = g_unk[i].ms; slot = i; }
    }
  }
  g_unk[slot].id = id;
  g_unk[slot].hasT = hasT;
  g_unk[slot].t = vt;
  g_unk[slot].ms = nowMs();
  answer = "OK (new sensor, add it on /settings/sensors)";
  return 202;
}

// ------------------------------------------------------------
// «Уличный» датчик -> общие переменные информера
// ------------------------------------------------------------
void localSensorsTick() {
  LsLock l;
  for (int i = 0; i < LS_SLOTS; i++) {
    const LsSlot &s = g_cfg.slots[i];
    if (!s.outdoor || !s.id.length()) continue;
    const LsVal &v = g_vals[i];
    if (v.hasT && isFresh(v, g_cfg.staleMin)) {
      outdoorTemp = v.t + s.offT;
      outdoorTempValid = true;
    }
    return;   // «уличный» может быть только один
  }
}

// ------------------------------------------------------------
// Для экрана «Комнаты»
// ------------------------------------------------------------
bool localSensorsRoomsEnabled() {
  LsLock l;
  return g_cfg.screenOn;
}

static int shownIndexLocked(int n) {
  int k = 0;
  for (int i = 0; i < LS_SLOTS; i++) {
    const LsSlot &s = g_cfg.slots[i];
    if (!s.id.length() || !s.screen) continue;
    if (k == n) return i;
    k++;
  }
  return -1;
}

static int realShownCountLocked() {
  int k = 0;
  for (int i = 0; i < LS_SLOTS; i++) {
    if (g_cfg.slots[i].id.length() && g_cfg.slots[i].screen) k++;
  }
  return k;
}

// Демо-датчики (для просмотра экрана «Комнаты» без настоящих датчиков)
static const int LS_DEMO_COUNT = 4;
static bool demoView(int n, LsView &out) {
  LsView r;
  r.fresh = true;
  r.hasT = true;
  switch (n) {
    case 0: r.name = "Лоджия"; r.t = 21.5f;  r.hasH = true; r.h = 45;  r.hasP = true; r.pMm = 745; break;
    case 1: r.name = "Улица";  r.t = -3.4f;  r.hasH = true; r.h = 82;  r.hasP = true; r.pMm = 745; break;
    case 2: r.name = "Кухня";  r.t = 24.0f;  r.hasH = true; r.h = 38;  r.hasP = true; r.pMm = 746; r.second = 2; break;   // во второй строке давление
    case 3: r.name = "Детская"; r.t = 22.3f; r.hasH = true; r.h = 51;  r.lowBatt = true; break;                              // красная точка «разряд»
    default: return false;
  }
  out = r;
  return true;
}

int localSensorsShownCount() {
  LsLock l;
  int k = realShownCountLocked();
  if (k == 0 && g_cfg.demo) return LS_DEMO_COUNT;
  return k;
}

bool localSensorsGetShown(int n, LsView &out) {
  LsLock l;
  if (g_cfg.demo && realShownCountLocked() == 0) return demoView(n, out);
  int i = shownIndexLocked(n);
  if (i < 0) return false;
  const LsSlot &s = g_cfg.slots[i];
  const LsVal &v = g_vals[i];
  LsView r;
  r.name = s.name.length() ? s.name : s.id;
  r.fresh = isFresh(v, g_cfg.staleMin);
  r.hasT = v.hasT;
  r.hasH = v.hasH;
  r.hasP = v.hasP;
  r.t = v.t + s.offT;
  r.h = clampf(v.h + s.offH, 0.0f, 100.0f);
  r.pMm = v.p * 0.750062f + s.offP;
  r.lowBatt = g_cfg.lowBatt > 0 && v.hasB && v.b < g_cfg.lowBatt && isFresh(v, g_cfg.staleMin * 3);
  r.second = s.second;
  out = r;
  return true;
}

// Датчик, выбранный источником для экрана «Дома», не дублируем на «Комнатах».
static bool slotIsHomeLocked(int i) {
  return g_cfg.homeSrc == i + 1 && g_cfg.slots[i].id.length();
}

static int roomsCountLocked() {
  int k = 0;
  for (int i = 0; i < LS_SLOTS; i++) {
    if (g_cfg.slots[i].id.length() && g_cfg.slots[i].screen && !slotIsHomeLocked(i)) k++;
  }
  return k;
}

int localSensorsRoomsCount() {
  LsLock l;
  int k = roomsCountLocked();
  if (k == 0 && g_cfg.demo) return LS_DEMO_COUNT;
  return k;
}

bool localSensorsRoomsGet(int n, LsView &out) {
  LsLock l;
  if (g_cfg.demo && roomsCountLocked() == 0) return demoView(n, out);
  int k = 0;
  for (int i = 0; i < LS_SLOTS; i++) {
    const LsSlot &s = g_cfg.slots[i];
    if (!s.id.length() || !s.screen || slotIsHomeLocked(i)) continue;
    if (k++ != n) continue;
    const LsVal &v = g_vals[i];
    LsView r;
    r.name = s.name.length() ? s.name : s.id;
    r.fresh = isFresh(v, g_cfg.staleMin);
    r.hasT = v.hasT;
    r.hasH = v.hasH;
    r.hasP = v.hasP;
    r.t = v.t + s.offT;
    r.h = clampf(v.h + s.offH, 0.0f, 100.0f);
    r.pMm = v.p * 0.750062f + s.offP;
    r.lowBatt = g_cfg.lowBatt > 0 && v.hasB && v.b < g_cfg.lowBatt && isFresh(v, g_cfg.staleMin * 3);
    r.second = s.second;
    out = r;
    return true;
  }
  return false;
}

// ------------------------------------------------------------
// Веб-страница /settings/sensors и приём /api/sensor
// ------------------------------------------------------------
static String esc(const String &s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static String fnum(float v, int dec) {
  char b[16];
  snprintf(b, sizeof(b), "%.*f", dec, v);
  return String(b);
}

static String statusOf(int i) {   // под блокировкой
  const LsVal &v = g_vals[i];
  if (v.ms == 0) return "данных ещё нет";
  String t;
  if (v.hasT) t += fnum(v.t, 1) + " °C";
  if (v.hasH) t += (t.length() ? ", " : "") + fnum(v.h, 0) + " %";
  if (v.hasP) t += (t.length() ? ", " : "") + fnum(v.p * 0.750062f, 0) + " мм рт. ст.";
  if (v.hasB) t += (t.length() ? ", " : "") + fnum(v.b, 2) + " В";
  unsigned long min = (millis() - v.ms) / 60000UL;
  t += " (" + String(min == 0 ? String("только что") : String(min) + " мин назад") + ")";
  if (!isFresh(v, g_cfg.staleMin)) t += " — устарело";
  return t;
}

static String msgText(const String &code) {
  if (code == "ok") return "Сохранено.";
  return "";
}


// Потоковая сборка страницы: пишем сразу в ответ, без второй копии в памяти
// (страница датчиков ~15 КБ, на ESP32 без PSRAM копия не помещалась).
struct HtmlStream {
  AsyncResponseStream *out = nullptr;
  String *buf = nullptr;
  explicit HtmlStream(AsyncResponseStream *o) : out(o) {}
  explicit HtmlStream(String *b) : buf(b) {}
  void reserve(size_t n) { if (buf) buf->reserve(n); }
  unsigned length() const { return buf ? (unsigned)buf->length() : 0; }
  HtmlStream &operator+=(const String &v) {
    if (buf) *buf += v; else if (out) out->print(v);
    return *this;
  }
  HtmlStream &operator+=(const char *v) {
    if (buf) *buf += v; else if (out) out->print(v);
    return *this;
  }
  HtmlStream &operator+=(char c) {
    if (buf) *buf += c; else if (out) out->write((uint8_t)c);
    return *this;
  }
};

static void buildPage(HtmlStream &h, const String &msgCode, int slotFrom, int slotTo) {
  LsConfig c;
  String status[LS_SLOTS];
  LsUnknown unk[LS_UNKNOWN_MAX];
  {
    LsLock l;
    c = g_cfg;
    for (int i = 0; i < LS_SLOTS; i++) status[i] = statusOf(i);
    for (int i = 0; i < LS_UNKNOWN_MAX; i++) unk[i] = g_unk[i];
  }
  String ip = WiFi.localIP().toString();

  h += "<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
       "<title>Датчики — PixelHub 75</title><link rel=\"icon\" type=\"image/png\" href=\"/favicon.ico\"><link rel=\"stylesheet\" href=\"/style.css\"></head><body><div class=\"wrap\">"
       "<div style=\"text-align:center;margin:10px 0 6px\"><img src=\"/logo.png\" alt=\"PixelHub 75\" style=\"width:88px;height:88px;border-radius:18px\"></div><div class=\"nav\"><a href=\"/\">Главная</a><a href=\"/settings/clock\">Часы</a><a href=\"/settings/scroll\">Бегущая строка</a>"
       "<a href=\"/settings/calendar\">Календарь</a><a href=\"/settings/sensors\">Датчики</a>"
       "<a href=\"/settings/weather\">Погода</a><a href=\"/settings/bigclock\">Большие часы</a>"
       "<a href=\"/settings/night\">Ночь/утро</a><a href=\"/settings/integrations\">Интеграции</a>"
       "<a href=\"/settings/brightness\">Яркость</a><a href=\"/settings/wifi\">Wi-Fi</a></div>"
       "<h1>Датчики по Wi-Fi</h1>"
       "<p class=\"hint\">Датчики присылают данные сами: <b>http://" + ip + "/api/sensor?id=имя&amp;t=21.5&amp;h=45</b></p>";

  String msg = msgText(msgCode);
  if (msg.length()) h += "<div class=\"card\"><b>" + esc(msg) + "</b></div>";

  bool anyUnknown = false;
  for (int i = 0; i < LS_UNKNOWN_MAX; i++) if (unk[i].id.length()) anyUnknown = true;
  if (anyUnknown) {
    h += "<div class=\"card\"><div class=\"line-title\">Обнаружены новые датчики</div><p class=\"hint\" style=\"margin:0\">";
    int unkShown = 0;
    for (int i = 0; i < LS_UNKNOWN_MAX; i++) {
      if (!unk[i].id.length()) continue;
      if (unkShown++ >= 2) break;
      h += "<b>" + esc(unk[i].id) + "</b>";
      if (unk[i].hasT) h += " (" + fnum(unk[i].t, 1) + " °C)";
      h += "; ";
    }
    h += "впишите нужное имя в свободный слот ниже и сохраните.</p></div>";
  }

  h += "<p class=\"hint\" style=\"margin:8px 0\">Датчики: ";
  for (int i = 0; i < LS_SLOTS; i++) {
    if (i) h += " | ";
    h += "<a href=\"/settings/sensors?p=" + String(i + 1) + "\">" + String(i + 1) + "</a>";
  }
  h += "</p>";
  h += "<form method=\"POST\" action=\"/settings/sensors\">";
  h += "<div class=\"card\"><div class=\"line-title\">Общие настройки</div>"
       "<div class=\"checkbox-row\"><input type=\"checkbox\" id=\"scrOn\" name=\"scrOn\"";
  if (c.screenOn) h += " checked";
  h += "><label for=\"scrOn\" style=\"margin:0\">Показывать экран «Комнаты» (по два датчика на экран)</label></div>"
       "<div class=\"checkbox-row\"><input type=\"checkbox\" id=\"demo\" name=\"demo\"";
  if (c.demo) h += " checked";
  h += "><label for=\"demo\" style=\"margin:0\">Демо: показывать «Комнаты» с тестовыми значениями, пока нет настоящих датчиков (4 комнаты)</label></div>"
       "<label for=\"stale\">Считать данные устаревшими через (минут)</label>"
       "<input type=\"number\" id=\"stale\" name=\"stale\" min=\"1\" max=\"1440\" value=\"" + String(c.staleMin) + "\">"
       "<label for=\"lowb\">Предупреждать о разряде батарейки ниже (В, 0 — не предупреждать)</label>"
       "<input type=\"number\" id=\"lowb\" name=\"lowb\" min=\"0\" max=\"6\" step=\"0.1\" value=\"" + fnum(c.lowBatt, 1) + "\">"
       "<label for=\"key\">Пароль в запросе датчика (необязательно: добавьте &amp;k=пароль в адрес)</label>"
       "<input type=\"text\" id=\"key\" name=\"key\" maxlength=\"16\" value=\"" + esc(c.key) + "\">"
       "<label for=\"hs\">Источник для экрана «Дома»</label>"
       "<select id=\"hs\" name=\"hs\"><option value=\"0\"" + String(c.homeSrc == 0 ? " selected" : "") + ">Встроенный датчик (I2C)</option>";
  for (int i = 0; i < LS_SLOTS; i++) {
    if (!c.slots[i].id.length()) continue;
    h += "<option value=\"" + String(i + 1) + "\"" + String(c.homeSrc == i + 1 ? " selected" : "") + ">";
    h += esc(c.slots[i].name.length() ? c.slots[i].name : c.slots[i].id);
    h += "</option>";
  }
  h += "</select></div>";

  for (int i = slotFrom; i < slotTo; i++) {
    const LsSlot &s = c.slots[i];
    String n = String(i);
    h += "<div class=\"card\"><div class=\"line-title\">Датчик " + String(i + 1) + "</div>";
    h += "<p class=\"hint\" style=\"margin:0 0 6px\">" + esc(status[i]) + "</p>";
    h += "<label>Имя датчика (латиницей, как в его настройках), пусто — слот свободен</label>"
         "<input type=\"text\" name=\"id" + n + "\" maxlength=\"" + String(LS_ID_MAX) + "\" value=\"" + esc(s.id) + "\" placeholder=\"street\">";
    h += "<label>Название на экране (коротко, около 5 букв)</label>"
         "<input type=\"text\" name=\"nm" + n + "\" maxlength=\"" + String(LS_NAME_MAX_BYTES / 2) + "\" value=\"" + esc(s.name) + "\" placeholder=\"Улица\">";
    h += "<div class=\"checkbox-row\"><input type=\"checkbox\" id=\"sc" + n + "\" name=\"sc" + n + "\"";
    if (s.screen) h += " checked";
    h += "><label for=\"sc" + n + "\" style=\"margin:0\">Показывать на экране «Комнаты»</label></div>";
    h += "<div class=\"checkbox-row\"><input type=\"checkbox\" id=\"od" + n + "\" name=\"od" + n + "\"";
    if (s.outdoor) h += " checked";
    h += "><label for=\"od" + n + "\" style=\"margin:0\">Считать уличным (ночной экран, «Сейчас на улице», MQTT/Народмон)</label></div>";
    h += "<label>Вторая строка на экране</label><select name=\"sw" + n + "\">"
         "<option value=\"0\"" + String(s.second == 0 ? " selected" : "") + ">Авто (влажность, а если её нет — давление)</option>"
         "<option value=\"1\"" + String(s.second == 1 ? " selected" : "") + ">Влажность</option>"
         "<option value=\"2\"" + String(s.second == 2 ? " selected" : "") + ">Давление</option></select>";
    h += "<label>Поправки: температура (°C), влажность (%), давление (мм рт. ст.)</label>"
         "<div style=\"display:flex;gap:8px\">"
         "<input type=\"number\" step=\"0.1\" min=\"-20\" max=\"20\" name=\"ot" + n + "\" value=\"" + fnum(s.offT, 1) + "\">"
         "<input type=\"number\" step=\"1\" min=\"-30\" max=\"30\" name=\"oh" + n + "\" value=\"" + fnum(s.offH, 0) + "\">"
         "<input type=\"number\" step=\"1\" min=\"-50\" max=\"50\" name=\"op" + n + "\" value=\"" + fnum(s.offP, 0) + "\"></div>";
    h += "</div>";
  }
  h += "</form></div></body></html>";
  }


static const String *paramPtr(AsyncWebServerRequest *req, const char *name, String &store) {
  if (req->hasParam(name)) { store = req->getParam(name)->value(); return &store; }
  if (req->hasParam(name, true)) { store = req->getParam(name, true)->value(); return &store; }
  return nullptr;
}

void localSensorsRegisterWeb(AsyncWebServer &srv) {
  // Приём значений: подходит и GET (удобно для ESP8266), и POST
  auto apiHandler = [](AsyncWebServerRequest *req) {
    String sid, sk, st, sh, sp, sb;
    const String *id = paramPtr(req, "id", sid);
    const String *k = paramPtr(req, "k", sk);
    const String *t = paramPtr(req, "t", st);
    const String *h = paramPtr(req, "h", sh);
    const String *p = paramPtr(req, "p", sp);
    const String *b = paramPtr(req, "b", sb);
    String answer;
    int code = receiveValues(id ? *id : String(""), k ? *k : String(""), t, h, p, b, answer);
    req->send(code == 202 ? 200 : code, "text/plain; charset=utf-8", answer);
  };
  srv.on("/api/sensor", HTTP_GET, apiHandler);
  srv.on("/api/sensor", HTTP_POST, apiHandler);

  srv.on("/settings/sensors", HTTP_GET, [](AsyncWebServerRequest *req) {
    String code = req->hasParam("msg") ? req->getParam("msg")->value() : String("");
    // 4 датчика, по 2 на страницу: страница целиком влезает в ответ веб-сервера
    int pageNo = req->hasParam("p") ? req->getParam("p")->value().toInt() : 1;
    if (pageNo < 1) pageNo = 1;
    if (pageNo > LS_SLOTS) pageNo = LS_SLOTS;
    int from = pageNo - 1;      // один датчик на страницу - страница меньше и влезает в память
    int to = from + 1;
    if (to > LS_SLOTS) to = LS_SLOTS;
    pageMemBegin();
    String page;
    page.reserve(ESP.getMaxAllocHeap() > 11000 ? 10000 : 7000);
    HtmlStream out(&page);
    buildPage(out, code, from, to);
    sendHtmlChunked(req, page);
    pageMemEnd();
  });

  srv.on("/settings/sensors", HTTP_POST, [](AsyncWebServerRequest *req) {
    auto P = [&](const String &n, const String &def) -> String {
      return req->hasParam(n, true) ? req->getParam(n, true)->value() : def;
    };
    {
      LsLock l;
      LsConfig c = g_cfg;
      c.screenOn = req->hasParam("scrOn", true);
      c.demo = req->hasParam("demo", true);
      c.staleMin = constrain(P("stale", String(c.staleMin)).toInt(), 1, 1440);
      c.lowBatt = clampf(P("lowb", String(c.lowBatt)).toFloat(), 0.0f, 6.0f);
      c.homeSrc = (int8_t)constrain(P("hs", String(c.homeSrc)).toInt(), 0, LS_SLOTS);
      String key = P("key", c.key);
      key.trim();
      key.replace("&", "");
      key.replace("\"", "");
      key.replace("<", "");
      key.replace(">", "");
      c.key = utf8Cut(key, 16);
      bool outdoorTaken = false;
      for (int i = 0; i < LS_SLOTS; i++) {
        String n = String(i);
        // Страница датчиков разбита по 2 слота: обновляем только те,
        // чьи поля реально пришли в форме, иначе сохранение одной
        // страницы очищало бы остальные.
        bool hasSlot = req->hasParam("id" + n, true);
        if (!hasSlot) continue;
        LsSlot &s = c.slots[i];
        String oldId = s.id;
        s.id = localSensorsCleanId(P("id" + n, ""));
        s.name = cleanName(P("nm" + n, ""));
        s.screen = req->hasParam("sc" + n, true);
        s.outdoor = req->hasParam("od" + n, true) && !outdoorTaken && s.id.length();
        if (s.outdoor) outdoorTaken = true;
        s.second = (uint8_t)constrain(P("sw" + n, "0").toInt(), 0, 2);
        s.offT = clampf(P("ot" + n, "0").toFloat(), -20.0f, 20.0f);
        s.offH = clampf(P("oh" + n, "0").toFloat(), -30.0f, 30.0f);
        s.offP = clampf(P("op" + n, "0").toFloat(), -50.0f, 50.0f);
        if (s.id != oldId) g_vals[i] = LsVal();   // другой датчик в слоте - старые значения не показываем
      }
      g_cfg = c;
      saveLocked();
    }
    req->redirect("/settings/sensors?msg=ok");
  });
}

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Wi-Fi датчики (Кухня, Улица, комнаты) присылают данные в домашнюю сеть.
// Матрица забирает их с домашней станции: дома — напрямую по HTTP,
// вне дома — через tuna (HTTPS). Так экран «Комнаты» и бегущая строка
// работают одинаково в обеих сборках.
void localSensorsRemoteTick() {
  static uint32_t last = 0;
  const uint32_t INTERVAL_MS = 60000;
  if (last != 0 && millis() - last < INTERVAL_MS) return;
  last = millis();

  String body;
#if defined(PIXELHUB_REMOTE)
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(12);
  HTTPClient http;
  http.setTimeout(12000);
  http.setReuse(false);
  if (!http.begin(client, "https://YOUR-TUNNEL.example.com/station/api/state")) return;
  http.addHeader("User-Agent", "PixelHub75-remote");
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); client.stop(); return; }
  body = http.getString();
  http.end();
  client.stop();
#else
  WiFiClient client;
  client.setTimeout(8);
  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  if (!http.begin(client, "http://YOUR-STATION/api/state")) return;
  http.addHeader("User-Agent", "PixelHub75");
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); client.stop(); return; }
  body = http.getString();
  http.end();
  client.stop();
#endif

  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  JsonArray list = doc["sensors"]["list"];
  int n = 0;
  for (JsonObject s : list) {
    String id = s["id"] | "";
    if (!id.length()) continue;
    String t = s["temp"].isNull() ? String() : String(s["temp"].as<float>(), 1);
    String h = s["humidity"].isNull() ? String() : String(s["humidity"].as<float>(), 0);
    String p = s["pressure_hpa"].isNull() ? String() : String(s["pressure_hpa"].as<float>(), 1);
    String answer;
    if (receiveValues(id, "", t.length()?&t:nullptr, h.length()?&h:nullptr,
                      p.length()?&p:nullptr, nullptr, answer) == 202) n++;
  }
  if (n) Serial.printf("Датчики с сервера: обновлено %d\n", n);
}

// Источник для экрана «Дома»: если выбран Wi-Fi датчик и данные свежие,
// отдаём его значения (температура °C, влажность %, давление гПа).
bool localSensorsHome(float &t, float &h, float &p) {
  LsLock l;
  const int idx = g_cfg.homeSrc;
  if (idx < 1 || idx > LS_SLOTS) return false;
  const LsSlot &s = g_cfg.slots[idx - 1];
  if (!s.id.length()) return false;
  const LsVal &v = g_vals[idx - 1];
  if (!v.ms) return false;
  const unsigned long age = nowMs() - v.ms;
  if (age > (unsigned long)g_cfg.staleMin * 60000UL) return false;

  bool any = false;
  t = NAN; h = NAN; p = NAN;
  if (v.hasT) { t = v.t + s.offT; any = true; }
  if (v.hasH) { h = v.h + s.offH; any = true; }
  if (v.hasP) { p = v.p + s.offP / 0.750062f; any = true; }   // поправка задана в мм рт.ст.
  return any;
}

// Выбран ли Wi-Fi датчик источником для экрана «Дома».
bool localSensorsHomeConfigured() {
  LsLock l;
  int idx = g_cfg.homeSrc;
  return idx >= 1 && idx <= LS_SLOTS && g_cfg.slots[idx - 1].id.length() > 0;
}
