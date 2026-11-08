#include "event_calendar.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "web_send.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <algorithm>
#include <SD.h>
#include "globals.h"
#include "utils.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ------------------------------------------------------------
// Настройки, которые удобно менять
// ------------------------------------------------------------
static const int CAL_MAX_EVENTS = 60;          // сколько событий можно хранить (без SD-карты ограничено ещё и размером NVS)
static const int CAL_TEXT_MAX_BYTES = 120;     // длина названия события в байтах (кириллица = 2 байта на букву, 120 = около 60 букв)
static const int CAL_MAX_SHOWN = 8;            // сколько событий максимум показывать в строке сразу
static const int CAL_MAX_REMIND = 14;          // максимум "за сколько дней напоминать"
static const float CAL_WAVE_DEG_PER_LETTER = 25.0f;   // <-- сдвиг оттенка от буквы к букве (меньше - плавнее и шире)
static const float CAL_WAVE_DEG_PER_MS = 0.18f;       // <-- скорость переливания (как на заставке)

// Хранение во флеш-памяти (NVS): JSON кусками по 1000 байт
static const char *CAL_NVS_NS = "calendar";
static const size_t CAL_CHUNK = 1000;
static const int CAL_MAX_CHUNKS = 4;           // до 4000 байт JSON во флеш-памяти (NVS мала, ей ещё хранить копию настроек)
static const size_t CAL_NVS_MAX = 3600;        // больше этого в NVS не пишем (остальное - только на SD)
static const size_t CAL_JSON_LIMIT = 16000;    // общий предел размера списка (SD-карта)
static const char *CAL_SD_FILE = "/calendar.json";
static const char *CAL_SD_TMP = "/calendar.new";

// ------------------------------------------------------------
// Состояние
// ------------------------------------------------------------
static CalConfig g_cfg;
static std::vector<CalEvent> g_events;
static SemaphoreHandle_t g_mux = nullptr;
static bool g_dirty = true;                    // нужна пересборка строки

static std::vector<CalSeg> g_segs;             // текущая строка (трогает только основной цикл)
static String g_rawLine;
static int g_lineChars = 0;
static int g_lastKey = -1;
static int g_speedCache = 5;
static uint32_t g_ver = 0;                     // номер версии списка: при расхождении NVS и SD побеждает больший
static uint32_t g_parsedVer = 0;               // версия, прочитанная последним calendarFromJson
static bool g_sdSynced = false;                // копия на SD сверена с NVS (делается один раз, когда карта появилась)
static bool g_sdOk = false;                    // последняя запись на SD удалась

struct CalLock {
  CalLock() { if (g_mux) xSemaphoreTake(g_mux, portMAX_DELAY); }
  ~CalLock() { if (g_mux) xSemaphoreGive(g_mux); }
};

// ------------------------------------------------------------
// Даты
// ------------------------------------------------------------
static bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static int daysInMonth(int y, int m) {
  static const uint8_t dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 0;
  return (m == 2 && isLeap(y)) ? 29 : dm[m - 1];
}

// Число дней от 1970-01-01 (алгоритм days_from_civil)
static long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

int calendarDaysUntil(const CalEvent &e, int y, int m, int d) {
  long today = daysFromCivil(y, m, d);
  if (e.y != 0) {
    long diff = daysFromCivil(e.y, e.m, e.d) - today;
    return diff < 0 ? -1 : (int)diff;
  }
  for (int yy = y; yy <= y + 1; yy++) {
    int dd = e.d;
    if (e.m == 2 && e.d == 29 && !isLeap(yy)) dd = 28;   // 29 февраля в обычный год - 28-го
    long diff = daysFromCivil(yy, e.m, dd) - today;
    if (diff >= 0) return (int)diff;
  }
  return -1;
}

String calendarDayWord(int n) {
  int n100 = n % 100, n10 = n % 10;
  if (n10 == 1 && n100 != 11) return "день";
  if (n10 >= 2 && n10 <= 4 && (n100 < 12 || n100 > 14)) return "дня";
  return "дней";
}

// ------------------------------------------------------------
// Праздники России (повторяются каждый год)
// ------------------------------------------------------------
struct Holiday { uint8_t d, m; const char *name; };
static const Holiday HOLIDAYS[] = {
  {1, 1, "Новый год"},
  {7, 1, "Рождество Христово"},
  {14, 2, "День святого Валентина"},
  {23, 2, "День защитника Отечества"},
  {8, 3, "Международный женский день"},
  {1, 5, "Праздник Весны и Труда"},
  {9, 5, "День Победы"},
  {12, 6, "День России"},
  {1, 9, "День знаний"},
  {4, 11, "День народного единства"},
  {31, 12, "Канун Нового года"},
};

// ------------------------------------------------------------
// Текст
// ------------------------------------------------------------
static String utf8Cut(const String &s, unsigned maxBytes) {
  if (s.length() <= maxBytes) return s;
  unsigned cut = maxBytes;
  while (cut > 0 && (((unsigned char)s[cut]) & 0xC0) == 0x80) cut--;
  return String(s.c_str(), cut);
}

// Оставляет то, что умеет шрифт: латиница, цифры, знаки, кириллица (2 байта D0/D1).
static String cleanEventText(const String &in) {
  String o;
  unsigned n = in.length();
  for (unsigned i = 0; i < n; i++) {
    unsigned char c = (unsigned char)in[i];
    if (c >= 0x20 && c <= 0x7E) {
      if (c == '<' || c == '>' || c == '"' || c == '&') continue;   // лишнее для страницы и JSON
      o += (char)c;
    } else if ((c == 0xD0 || c == 0xD1) && i + 1 < n) {
      o += (char)c;
      o += in[++i];
    }
  }
  // убираем пробелы по краям
  while (o.length() && o[0] == ' ') o = String(o.c_str() + 1);
  while (o.length() && o[o.length() - 1] == ' ') o = String(o.c_str(), o.length() - 1);
  return utf8Cut(o, CAL_TEXT_MAX_BYTES);
}

static String prefixFor(int days) {
  if (days <= 0) return "Сегодня: ";
  if (days == 1) return "Завтра: ";
  if (days == 2) return "Послезавтра: ";
  return "Через " + String(days) + " " + calendarDayWord(days) + ": ";
}

// Склонение: 1 год, 2 года, 5 лет
String calendarYearWord(int n) {
  int n100 = n % 100, n10 = n % 10;
  if (n10 == 1 && n100 != 11) return "год";
  if (n10 >= 2 && n10 <= 4 && (n100 < 12 || n100 > 14)) return "года";
  return "лет";
}

// Возраст/годовщина на ближайшее наступление события: 0 - не показывать
int calendarAgeOn(const CalEvent &e, int y, int m, int d) {
  if (e.by == 0 || e.y != 0) return 0;
  int occ = y;
  // событие в этом году уже прошло - ближайшее будет в следующем
  if (e.m < m || (e.m == m && e.d < d)) occ = y + 1;
  int age = occ - (int)e.by;
  return age > 0 ? age : 0;
}

struct Item { int days; int order; String text; bool rainbow; uint32_t color; };


#if !defined(PIXELHUB_REMOTE)
// ---- события и праздники с домашнего сервера ----
static std::vector<CalEvent> g_remote;
static unsigned long g_remoteLast = 0;

static void calendarFetchRemote() {
  if (WiFi.status() != WL_CONNECTED) return;
  const unsigned long nowMs = millis();
  if (g_remoteLast && nowMs - g_remoteLast < 6UL * 3600UL * 1000UL) return;   // раз в 6 часов
  g_remoteLast = nowMs;

  WiFiClient client;
  client.setTimeout(6);
  HTTPClient http;
  http.setTimeout(6000);
  http.setReuse(false);
  if (!http.begin(client, "http://YOUR-SERVER:8082/calendar")) return;
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); client.stop(); return; }
  String body = http.getString();
  http.end();
  client.stop();

  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  std::vector<CalEvent> tmp;
  for (JsonObject o : doc["events"].as<JsonArray>()) {
    CalEvent e;
    e.d = (uint8_t)constrain((int)(o["d"] | 1), 1, 31);
    e.m = (uint8_t)constrain((int)(o["m"] | 1), 1, 12);
    e.y = (uint16_t)(o["y"] | 0);
    e.text = String((const char *)(o["t"] | ""));
    e.rainbow = true;
    e.color = 0xFFFFFF;
    if (e.text.length()) tmp.push_back(e);
  }
  {
    CalLock l;
    g_remote = tmp;
    g_dirty = true;
  }
  Serial.printf("Календарь: с сервера %d событий\n", (int)tmp.size());
}
#endif

void calendarBuildSegments(const CalConfig &cfg, const std::vector<CalEvent> &events,
                           int y, int m, int d, std::vector<CalSeg> &out) {
  out.clear();
  if (!cfg.enabled || y < 2024) return;
  std::vector<Item> items;
  int order = 0;
  for (const CalEvent &e : events) {
    int n = calendarDaysUntil(e, y, m, d);
    if (n >= 0 && n <= cfg.remindDays) {
      String t = e.text;
      int age = calendarAgeOn(e, y, m, d);
      if (age > 0) t += " (" + String(age) + " " + calendarYearWord(age) + ")";
      items.push_back({n, order, t, e.rainbow, e.color});
    }
    order++;
  }
  const bool useRemote = false
#if !defined(PIXELHUB_REMOTE)
      || cfg.remote
#endif
      ;
#if !defined(PIXELHUB_REMOTE)
  if (useRemote) {
    for (const CalEvent &e : g_remote) {
      int n = calendarDaysUntil(e, y, m, d);
      if (n >= 0 && n <= cfg.remindDays) items.push_back({n, order, e.text, true, 0xFFFFFF});
      order++;
    }
  }
#endif
  if (cfg.holidays && !useRemote) {
    for (const Holiday &h : HOLIDAYS) {
      CalEvent e;
      e.d = h.d; e.m = h.m; e.y = 0;
      int n = calendarDaysUntil(e, y, m, d);
      if (n >= 0 && n <= cfg.remindDays) items.push_back({n, order, String(h.name), true, 0xFFFFFF});
      order++;
    }
  }
  std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
    return a.days != b.days ? a.days < b.days : a.order < b.order;
  });
  int shown = 0;
  for (const Item &it : items) {
    if (shown++ >= CAL_MAX_SHOWN) break;
    CalSeg s;
    s.raw = prefixFor(it.days) + it.text + "   ";
    s.conv = utf8rus(s.raw);
    s.rainbow = it.rainbow;
    s.color = it.color;
    out.push_back(s);
  }
}

// ------------------------------------------------------------
// JSON и хранение
// ------------------------------------------------------------
String calendarToJson(const CalConfig &cfg, const std::vector<CalEvent> &events) {
  JsonDocument doc;
  doc["e"] = cfg.enabled ? 1 : 0;
  doc["r"] = cfg.remindDays;
  doc["h"] = cfg.holidays ? 1 : 0;
  doc["rs"] = cfg.remote ? 1 : 0;
  doc["s"] = cfg.speed;
  doc["n"] = g_ver;
  JsonArray arr = doc["v"].to<JsonArray>();
  for (const CalEvent &e : events) {
    JsonObject o = arr.add<JsonObject>();
    o["d"] = e.d;
    o["m"] = e.m;
    o["y"] = e.y;
    o["w"] = e.rainbow ? 1 : 0;
    o["c"] = e.color;
    if (e.by) o["b"] = e.by;
    o["t"] = e.text.c_str();
  }
  String out;
  serializeJson(doc, out);
  return out;
}

static void sortEvents(std::vector<CalEvent> &ev) {
  std::stable_sort(ev.begin(), ev.end(), [](const CalEvent &a, const CalEvent &b) {
    if (a.m != b.m) return a.m < b.m;
    if (a.d != b.d) return a.d < b.d;
    return a.y < b.y;
  });
}

bool calendarFromJson(const String &json, CalConfig &cfg, std::vector<CalEvent> &events) {
  JsonDocument doc;
  if (deserializeJson(doc, json.c_str())) return false;
  CalConfig c;
  g_parsedVer = doc["n"] | 0;
  c.enabled = (doc["e"] | 1) != 0;
  c.remindDays = constrain((int)(doc["r"] | 2), 0, CAL_MAX_REMIND);
  c.holidays = (doc["h"] | 0) != 0;
  c.remote = (doc["rs"] | 1) != 0;
  c.speed = constrain((int)(doc["s"] | 5), 1, 10);
  std::vector<CalEvent> ev;
  for (JsonObject o : doc["v"].as<JsonArray>()) {
    if ((int)ev.size() >= CAL_MAX_EVENTS) break;
    CalEvent e;
    int m = o["m"] | 0, d = o["d"] | 0, y = o["y"] | 0;
    if (m < 1 || m > 12 || d < 1 || d > 31) continue;
    e.m = m; e.d = d; e.y = (y >= 2024 && y <= 2100) ? y : 0;
    int by = o["b"] | 0;
    e.by = (by >= 1900 && by <= 2100) ? by : 0;
    e.rainbow = (o["w"] | 1) != 0;
    e.color = (uint32_t)(o["c"] | 0xFFFFFFu) & 0xFFFFFF;
    e.text = cleanEventText(String((const char *)(o["t"] | "")));
    if (e.text.length() == 0) continue;
    ev.push_back(e);
  }
  sortEvents(ev);
  cfg = c;
  events = ev;
  return true;
}

static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

// Короткая подпись названия события (чтобы не пересылать текст второй раз при удалении)
static String textHash(const String &t) {
  char b[12];
  snprintf(b, sizeof(b), "%08x", (unsigned)crc32((const uint8_t *)t.c_str(), t.length()));
  return String(b);
}

// Возвращает true, если записано
static bool saveToNvs(const String &json) {
  size_t len = json.length();
  if (len == 0 || len > CAL_NVS_MAX) return false;
  Preferences p;
  if (!p.begin(CAL_NVS_NS, false)) return false;
  int chunks = (int)((len + CAL_CHUNK - 1) / CAL_CHUNK);
  bool ok = true;
  for (int i = 0; i < chunks && ok; i++) {
    char key[4];
    snprintf(key, sizeof(key), "c%d", i);
    size_t off = (size_t)i * CAL_CHUNK;
    size_t n = std::min(CAL_CHUNK, len - off);
    ok = p.putBytes(key, json.c_str() + off, n) == n;
  }
  if (ok) {
    p.putUInt("len", (uint32_t)len);
    p.putUInt("crc", crc32((const uint8_t *)json.c_str(), len));
  }
  p.end();
  return ok;
}

static bool loadFromNvs(String &json) {
  Preferences p;
  if (!p.begin(CAL_NVS_NS, true)) return false;
  size_t len = p.getUInt("len", 0);
  uint32_t crc = p.getUInt("crc", 0);
  bool ok = len > 0 && len <= CAL_NVS_MAX;
  std::vector<uint8_t> buf;
  if (ok) {
    buf.resize(len);
    int chunks = (int)((len + CAL_CHUNK - 1) / CAL_CHUNK);
    for (int i = 0; i < chunks && ok; i++) {
      char key[4];
      snprintf(key, sizeof(key), "c%d", i);
      size_t off = (size_t)i * CAL_CHUNK;
      size_t n = std::min(CAL_CHUNK, len - off);
      ok = p.getBytes(key, buf.data() + off, n) == n;
    }
  }
  p.end();
  if (!ok || crc32(buf.data(), len) != crc) return false;
  json = String((const char *)buf.data(), (unsigned)len);
  return true;
}

// ------------------------------------------------------------
// Копия на SD-карте (файл /calendar.json). Карта общая с будильником/радио (её открывает audio_engine).
// ------------------------------------------------------------
static bool sdReady() { return SD.cardType() != CARD_NONE; }

static bool sdWriteJson(const String &json) {
  if (!sdReady()) return false;
  File f = SD.open(CAL_SD_TMP, FILE_WRITE);
  if (!f) return false;
  size_t n = f.print(json);
  f.close();
  if (n != json.length()) { SD.remove(CAL_SD_TMP); return false; }
  SD.remove(CAL_SD_FILE);
  return SD.rename(CAL_SD_TMP, CAL_SD_FILE);
}

static bool sdReadJson(String &json) {
  if (!sdReady() || !SD.exists(CAL_SD_FILE)) return false;
  File f = SD.open(CAL_SD_FILE, FILE_READ);
  if (!f) return false;
  json = f.readString();
  f.close();
  return json.length() > 0 && json.length() <= CAL_JSON_LIMIT;
}

bool calendarSdActive() { return g_sdSynced && sdReady(); }

// Сохранить текущее состояние (вызывать под блокировкой).
// Успех, если записалось хотя бы в одно место: на SD (любой размер) или в NVS (если список небольшой).
static bool persistLocked() {
  g_dirty = true;
  g_ver++;
  String json = calendarToJson(g_cfg, g_events);
  bool sdOk = g_sdSynced ? sdWriteJson(json) : false;
  g_sdOk = sdOk;
  bool nvsOk = json.length() <= CAL_NVS_MAX ? saveToNvs(json) : false;
  return sdOk || nvsOk;
}

// Однократная сверка NVS и SD, когда карта появилась (вызывать под блокировкой)
static void syncSdLocked() {
  if (g_sdSynced || !sdReady()) return;
  g_sdSynced = true;
  String json;
  CalConfig c;
  std::vector<CalEvent> ev;
  bool haveSd = sdReadJson(json) && calendarFromJson(json, c, ev);
  uint32_t sdVer = g_parsedVer;
  if (haveSd && sdVer > g_ver) {
    g_cfg = c;
    g_events = ev;
    g_ver = sdVer;
    g_dirty = true;
    String j = calendarToJson(g_cfg, g_events);
    if (j.length() <= CAL_NVS_MAX) saveToNvs(j);
    Serial.println("📅 Календарь: взят список с SD-карты (событий " + String((int)g_events.size()) + ")");
  } else if (!haveSd || sdVer < g_ver) {
    g_sdOk = sdWriteJson(calendarToJson(g_cfg, g_events));
    Serial.println(g_sdOk ? "📅 Календарь: копия записана на SD" : "📅 Календарь: не удалось записать на SD");
  } else {
    g_sdOk = true;
  }
}

// ------------------------------------------------------------
// Жизненный цикл
// ------------------------------------------------------------
void calendarInit() {
  if (!g_mux) g_mux = xSemaphoreCreateMutex();
  String json;
  CalLock l;
  g_cfg = CalConfig();
  g_events.clear();
  g_ver = 0;
  g_sdSynced = false;
  g_sdOk = false;
  if (loadFromNvs(json) && calendarFromJson(json, g_cfg, g_events)) {
    g_ver = g_parsedVer;
    Serial.println("📅 Календарь: событий " + String((int)g_events.size()));
  } else {
    Serial.println("📅 Календарь: пока пусто");
  }
  g_dirty = true;
}

void calendarTick() {
#if !defined(PIXELHUB_REMOTE)
  calendarFetchRemote();
#endif
  if (!g_sdSynced) {
    static unsigned long lastTry = 0;
    if (lastTry == 0 || millis() - lastTry > 20000UL) {   // карта могла смонтироваться позже старта
      lastTry = millis() ? millis() : 1;
      CalLock l;
      syncSdLocked();
    }
  }
  if (year < 2024) return;               // время ещё не получено
  int key = year * 10000 + (monthNum + 1) * 100 + dayNum;
  if (!g_dirty && key == g_lastKey) return;
  CalConfig cfg;
  std::vector<CalEvent> ev;
  {
    CalLock l;
    cfg = g_cfg;
    ev = g_events;
    g_dirty = false;
  }
  g_lastKey = key;
  calendarBuildSegments(cfg, ev, year, monthNum + 1, dayNum, g_segs);
  String raw;
  int chars = 0;
  for (const CalSeg &s : g_segs) {
    raw += s.raw;
    chars += s.conv.length();
  }
  g_rawLine = raw;
  g_lineChars = chars;
  g_speedCache = cfg.speed;
  Serial.println("📅 Календарь: строка пересобрана (" + String((int)g_segs.size()) + " шт.)");
}

const String &calendarLineText() { return g_rawLine; }
int calendarLineChars() { return g_lineChars; }
int calendarLineSpeed() { return g_speedCache; }

// ------------------------------------------------------------
// Рисование: радуга по буквам
// ------------------------------------------------------------
static void hueToRgb8(float h, uint8_t &r, uint8_t &g, uint8_t &b) {
  h = fmodf(h, 360.0f);
  if (h < 0) h += 360.0f;
  float x = h / 60.0f;
  int i = (int)x;
  float f = x - i;
  float rr = 0, gg = 0, bb = 0;
  switch (i) {
    case 0: rr = 1;     gg = f;     bb = 0;     break;
    case 1: rr = 1 - f; gg = 1;     bb = 0;     break;
    case 2: rr = 0;     gg = 1;     bb = f;     break;
    case 3: rr = 0;     gg = 1 - f; bb = 1;     break;
    case 4: rr = f;     gg = 0;     bb = 1;     break;
    default: rr = 1;    gg = 0;     bb = 1 - f; break;
  }
  r = (uint8_t)(rr * 255.0f + 0.5f);
  g = (uint8_t)(gg * 255.0f + 0.5f);
  b = (uint8_t)(bb * 255.0f + 0.5f);
}

void calendarDrawLine(int x, int y) {
  display->setTextWrap(false);
  unsigned long t = millis();
  int letter = 0;
  for (const CalSeg &s : g_segs) {
    uint16_t fixedCol = display->color565((s.color >> 16) & 0xFF, (s.color >> 8) & 0xFF, s.color & 0xFF);
    for (unsigned i = 0; i < s.conv.length(); i++, letter++) {
      if (x >= 64) return;                       // правее экрана - дальше рисовать нечего
      uint16_t col = fixedCol;
      if (s.rainbow) {
        uint8_t r, g, b;
        hueToRgb8(letter * CAL_WAVE_DEG_PER_LETTER + t * CAL_WAVE_DEG_PER_MS, r, g, b);
        col = display->color565(r, g, b);
      }
      display->setTextColor(col);
      display->setCursor(x, y);
      display->write((uint8_t)s.conv[i]);
      x = display->getCursorX();
    }
  }
}

// ------------------------------------------------------------
// Веб-страница /settings/calendar
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

static String two(int v) { char b[8]; snprintf(b, sizeof(b), "%02d", v); return String(b); }

static String hex6(uint32_t c) { char b[12]; snprintf(b, sizeof(b), "#%06X", (unsigned)(c & 0xFFFFFF)); return String(b); }

static String msgText(const String &code) {
  if (code == "ok") return "Сохранено.";
  if (code == "full") return "Достигнут предел: " + String(CAL_MAX_EVENTS) + " событий. Удалите ненужные.";
  if (code == "baddate") return "Не удалось разобрать дату.";
  if (code == "notext") return "Введите название события (буквы, цифры, знаки).";
  if (code == "toobig") return "Список не помещается во флеш-память. Вставьте SD-карту или удалите часть событий.";
  if (code == "nosave") return "Не удалось сохранить (флеш-память и SD-карта).";
  return "";
}

static String buildCalendarPage(const String &msgCode) {
  CalConfig cfg;
  std::vector<CalEvent> ev;
  {
    CalLock l;
    cfg = g_cfg;
    ev = g_events;
  }
  bool timeOk = year >= 2024;
  std::vector<CalSeg> segs;
  if (timeOk) calendarBuildSegments(cfg, ev, year, monthNum + 1, dayNum, segs);
  String preview;
  for (const CalSeg &s : segs) preview += s.raw;

  String h;
  h.reserve(6000);
  h += "<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
       "<title>Календарь — PixelHub 75</title><link rel=\"stylesheet\" href=\"/style.css\">"
       "<style>.ev{display:flex;align-items:center;gap:8px;margin:8px 0;flex-wrap:wrap}.ev .dt{min-width:120px}.ev .tx{flex:1;min-width:140px}"
       ".ev .sw{width:22px;height:14px;border-radius:3px;display:inline-block}.ev .rb{background:linear-gradient(90deg,#f00,#ff0,#0f0,#0ff,#00f,#f0f)}"
       ".ev form{margin:0}.ev button{width:auto;padding:4px 12px}</style></head><body><div class=\"wrap\">"
       "<div class=\"nav\"><a href=\"/\">Главная</a><a href=\"/settings/clock\">Часы</a><a href=\"/settings/scroll\">Бегущая строка</a>"
       "<a href=\"/settings/calendar\">Календарь</a>"
       "<a href=\"/settings/weather\">Погода</a><a href=\"/settings/bigclock\">Большие часы</a>"
       "<a href=\"/settings/night\">Ночь/утро</a><a href=\"/settings/integrations\">Интеграции</a>"
       "<a href=\"/settings/brightness\">Яркость</a><a href=\"/settings/wifi\">Wi-Fi</a></div>"
       "<h1>Календарь событий</h1>"
       "<p class=\"hint\">События бегут в строке сразу после даты. За несколько дней до события появляется напоминание "
       "(«Завтра: День рождения мамы»), в сам день — «Сегодня:». События хранятся во флеш-памяти и не пропадают при обновлении файловой системы.</p>";

  String msg = msgText(msgCode);
  if (msg.length()) h += "<div class=\"card\"><b>" + esc(msg) + "</b></div>";

  // Сейчас на экране
  h += "<div class=\"card\"><div class=\"line-title\">Сейчас в строке</div><p class=\"hint\" style=\"margin:0\">";
  if (!timeOk) h += "Время ещё не получено из интернета.";
  else if (!cfg.enabled) h += "Строка календаря выключена.";
  else if (preview.length() == 0) h += "Ближайших событий нет — строка не показывается.";
  else h += esc(preview);
  h += "</p></div>";

  // Настройки
  h += "<form method=\"POST\" action=\"/settings/calendar\"><div class=\"card\"><div class=\"line-title\">Настройки</div>"
       "<div class=\"checkbox-row\"><input type=\"checkbox\" id=\"calOn\" name=\"calOn\"";
  if (cfg.enabled) h += " checked";
  h += "><label for=\"calOn\" style=\"margin:0\">Показывать календарь в бегущей строке</label></div>"
       "<label for=\"calRemind\">Напоминать за (дней, 0 — только в сам день)</label>"
       "<input type=\"number\" id=\"calRemind\" name=\"calRemind\" min=\"0\" max=\"" + String(CAL_MAX_REMIND) + "\" value=\"" + String(cfg.remindDays) + "\">"
       "<div class=\"checkbox-row\"><input type=\"checkbox\" id=\"calHol\" name=\"calHol\"";
  if (cfg.holidays) h += " checked";
  h += "><label for=\"calHol\" style=\"margin:0\">Добавлять государственные праздники России</label></div>";
#if !defined(PIXELHUB_REMOTE)
  h += "<div class=\"checkbox-row\"><input type=\"checkbox\" id=\"calRemote\" name=\"calRemote\"";
  if (cfg.remote) h += " checked";
  h += "><label for=\"calRemote\" style=\"margin:0\">Брать праздники и события с сервера (праздники РФ + свои)</label></div>";
#endif
       "<label for=\"calSpeed\">Скорость (1 — медленно, 10 — быстро)</label>"
       "<input type=\"range\" id=\"calSpeed\" name=\"calSpeed\" min=\"1\" max=\"10\" value=\"" + String(cfg.speed) + "\">"
       "<button type=\"submit\">Сохранить</button></div></form>";

  // Список событий
  h += "<div class=\"card\"><div class=\"line-title\">События (" + String((int)ev.size()) + " из " + String(CAL_MAX_EVENTS) + ")</div>"
       "<p class=\"hint\" style=\"margin:0 0 8px\">" + String(calendarSdActive() ? (g_sdOk ? "Копия на SD-карте: есть." : "SD-карта найдена, но запись не удалась.")
                                                                       : "SD-карта не найдена: события хранятся только во флеш-памяти (меньше места).") + "</p>";
  if (ev.empty()) h += "<p class=\"hint\" style=\"margin:0\">Пока нет ни одного события.</p>";
  bool anyPast = false;
  for (size_t i = 0; i < ev.size(); i++) {
    const CalEvent &e = ev[i];
    String date = two(e.d) + "." + two(e.m);
    if (e.y) date += "." + String((int)e.y); else date += " (каждый год)";
    String ageNote = e.by ? " [с " + String((int)e.by) + " г.]" : String("");
    bool past = timeOk && calendarDaysUntil(e, year, monthNum + 1, dayNum) < 0;
    if (past) anyPast = true;
    h += "<div class=\"ev\"><span class=\"dt\">" + esc(date) + "</span>";
    if (e.rainbow) h += "<span class=\"sw rb\"></span>";
    else h += "<span class=\"sw\" style=\"background:" + hex6(e.color) + ";border:1px solid #888\"></span>";
    h += "<span class=\"tx\">" + esc(e.text) + esc(ageNote) + (past ? " <i>(прошло)</i>" : "") + "</span>";
    h += "<form method=\"POST\" action=\"/settings/calendar_del\"><input type=\"hidden\" name=\"i\" value=\"" + String((int)i) +
         "\"><input type=\"hidden\" name=\"h\" value=\"" + textHash(e.text) + "\"><button type=\"submit\">Удалить</button></form></div>";
  }
  if (anyPast) {
    h += "<form method=\"POST\" action=\"/settings/calendar_clean\"><button type=\"submit\">Убрать прошедшие разовые события</button></form>";
  }
  h += "</div>";

  // Добавление
  h += "<form method=\"POST\" action=\"/settings/calendar_add\"><div class=\"card\"><div class=\"line-title\">Добавить событие</div>"
       "<label for=\"evDate\">Дата</label><input type=\"date\" id=\"evDate\" name=\"date\" required>"
       "<div class=\"checkbox-row\"><input type=\"checkbox\" id=\"evYearly\" name=\"yearly\" checked>"
       "<label for=\"evYearly\" style=\"margin:0\">Повторять каждый год (дни рождения, праздники)</label></div>"
       "<label for=\"evBy\">Год рождения или начала (необязательно: тогда в строке будет «(35 лет)»)</label>"
       "<input type=\"number\" id=\"evBy\" name=\"byear\" min=\"1900\" max=\"2100\" placeholder=\"1990\">"
       "<label for=\"evText\">Название (до " + String(CAL_TEXT_MAX_BYTES / 2) + " русских букв)</label>"
       "<input type=\"text\" id=\"evText\" name=\"text\" maxlength=\"" + String(CAL_TEXT_MAX_BYTES / 2) + "\" placeholder=\"День рождения мамы\" required>"
       "<div class=\"checkbox-row\"><input type=\"checkbox\" id=\"evRainbow\" name=\"rainbow\" checked>"
       "<label for=\"evRainbow\" style=\"margin:0\">Радужное переливание по буквам</label></div>"
       "<label for=\"evColor\">Цвет (если без радуги)</label><input type=\"color\" id=\"evColor\" name=\"color\" value=\"#ffffff\">"
       "<button type=\"submit\">Добавить</button></div></form>";

  h += "</div></body></html>";
  return h;
}

void calendarRegisterWeb(AsyncWebServer &srv) {
  srv.on("/settings/calendar", HTTP_GET, [](AsyncWebServerRequest *req) {
    String code = req->hasParam("msg") ? req->getParam("msg")->value() : String("");
    pageMemBegin();
    sendHtmlChunked(req, buildCalendarPage(code));
    pageMemEnd();
  });

  srv.on("/settings/calendar", HTTP_POST, [](AsyncWebServerRequest *req) {
    auto P = [&](const char *n, const String &def) -> String {
      return req->hasParam(n, true) ? req->getParam(n, true)->value() : def;
    };
    String code = "ok";
    {
      CalLock l;
      g_cfg.enabled = req->hasParam("calOn", true);
      g_cfg.holidays = req->hasParam("calHol", true);
#if !defined(PIXELHUB_REMOTE)
      g_cfg.remote = req->hasParam("calRemote", true);
#endif
      g_cfg.remindDays = constrain(P("calRemind", String((int)g_cfg.remindDays)).toInt(), 0, CAL_MAX_REMIND);
      g_cfg.speed = constrain(P("calSpeed", String((int)g_cfg.speed)).toInt(), 1, 10);
      if (!persistLocked()) code = "nosave";
    }
    req->redirect(("/settings/calendar?msg=" + code).c_str());
  });

  srv.on("/settings/calendar_add", HTTP_POST, [](AsyncWebServerRequest *req) {
    auto P = [&](const char *n) -> String {
      return req->hasParam(n, true) ? req->getParam(n, true)->value() : String("");
    };
    String date = P("date");   // ГГГГ-ММ-ДД
    int y = 0, m = 0, d = 0;
    String code = "ok";
    bool yearly = req->hasParam("yearly", true);
    if (date.length() != 10 || date[4] != '-' || date[7] != '-' ||
        sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 ||
        d > daysInMonth(yearly ? 2000 : y, m) || y < 2000 || y > 2100) {
      code = "baddate";
    }
    String text = cleanEventText(P("text"));
    if (code == "ok" && text.length() == 0) code = "notext";
    if (code == "ok") {
      CalEvent e;
      e.d = d; e.m = m; e.y = yearly ? 0 : y;
      e.rainbow = req->hasParam("rainbow", true);
      String col = P("color");
      e.color = (col.length() == 7 && col[0] == '#') ? (uint32_t)strtoul(col.c_str() + 1, nullptr, 16) & 0xFFFFFF : 0xFFFFFF;
      e.text = text;
      if (yearly) {
        int by = P("byear").toInt();
        e.by = (by >= 1900 && by <= 2100) ? by : 0;
      }
      CalLock l;
      if ((int)g_events.size() >= CAL_MAX_EVENTS) {
        code = "full";
      } else {
        std::vector<CalEvent> backup = g_events;
        g_events.push_back(e);
        sortEvents(g_events);
        size_t jl = calendarToJson(g_cfg, g_events).length();
        if (jl > CAL_JSON_LIMIT || (jl > CAL_NVS_MAX && !g_sdSynced)) {
          g_events = backup;
          code = "toobig";
        } else if (!persistLocked()) {
          code = "nosave";
        }
      }
    }
    req->redirect(("/settings/calendar?msg=" + code).c_str());
  });

  srv.on("/settings/calendar_del", HTTP_POST, [](AsyncWebServerRequest *req) {
    int i = req->hasParam("i", true) ? req->getParam("i", true)->value().toInt() : -1;
    String t = req->hasParam("t", true) ? req->getParam("t", true)->value() : String("");
    String hs = req->hasParam("h", true) ? req->getParam("h", true)->value() : String("");
    String code = "ok";
    {
      CalLock l;
      // Удаляем, только если под этим номером то же событие (список мог измениться)
      if (i >= 0 && i < (int)g_events.size() &&
          (hs.length() ? textHash(g_events[i].text) == hs : g_events[i].text == t)) {
        g_events.erase(g_events.begin() + i);
        if (!persistLocked()) code = "nosave";
      }
    }
    req->redirect(("/settings/calendar?msg=" + code).c_str());
  });

  srv.on("/settings/calendar_clean", HTTP_POST, [](AsyncWebServerRequest *req) {
    String code = "ok";
    if (year >= 2024) {
      CalLock l;
      std::vector<CalEvent> keep;
      for (const CalEvent &e : g_events) {
        if (calendarDaysUntil(e, year, monthNum + 1, dayNum) >= 0) keep.push_back(e);
      }
      if (keep.size() != g_events.size()) {
        g_events = keep;
        if (!persistLocked()) code = "nosave";
      }
    }
    req->redirect(("/settings/calendar?msg=" + code).c_str());
  });
}
