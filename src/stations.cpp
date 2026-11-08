#include "stations.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

static StationInfo stations[MAX_STATIONS];
static int stationCount = 0;

void loadStations() {
  stationCount = 0;
  File file = LittleFS.open("/stations.json", "r");
  if (!file) {
    Serial.println("ℹ️ /stations.json не найден - список станций пуст (добавьте через веб)");
    return;
  }
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    Serial.println("❌ Ошибка разбора /stations.json: " + String(error.c_str()));
    return;
  }
  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject obj : arr) {
    if (stationCount >= MAX_STATIONS) break;
    stations[stationCount].name = obj["name"] | "";
    stations[stationCount].url = obj["url"] | "";
    stationCount++;
  }
  Serial.println("✅ Загружено станций: " + String(stationCount));
}

void saveStations() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < stationCount; i++) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = stations[i].name;
    obj["url"] = stations[i].url;
  }
  File file = LittleFS.open("/stations.json", "w");
  if (!file) {
    Serial.println("❌ Не удалось открыть /stations.json на запись");
    return;
  }
  serializeJson(doc, file);
  file.close();
}

int getStationCount() {
  return stationCount;
}

StationInfo getStation(int index) {
  if (index < 0 || index >= stationCount) return StationInfo{"", ""};
  return stations[index];
}

bool addStation(const String &name, const String &url) {
  if (name.length() == 0 || url.length() == 0) return false;
  if (stationCount >= MAX_STATIONS) return false;
  stations[stationCount].name = name;
  stations[stationCount].url = url;
  stationCount++;
  saveStations();
  return true;
}

bool removeStation(int index) {
  if (index < 0 || index >= stationCount) return false;
  for (int i = index; i < stationCount - 1; i++) {
    stations[i] = stations[i + 1];
  }
  stationCount--;
  saveStations();
  return true;
}

int importPlaylist(const String &content) {
  int added = 0;
  int pos = 0;
  String pendingName = "";
  int debugLinesLeft = 3; // подробный лог только для первых 3 строк

  while (pos < (int)content.length()) {
    int nl = content.indexOf('\n', pos);
    String line = (nl == -1) ? content.substring(pos) : content.substring(pos, nl);
    line.trim();
    if (line.length() > 0 && line[line.length() - 1] == '\r') {
      line.remove(line.length() - 1);
    }

    if (debugLinesLeft > 0) {
      Serial.println("[парсер] строка: \"" + line + "\" (длина " + String(line.length()) + ")");
      Serial.println("[парсер] indexOf('\\t') = " + String(line.indexOf('\t')));
    }

    if (line.indexOf('\t') != -1) {
      // Формат "Ё-радио" (yoRadio/VTomRadio): "Имя<TAB>URL<TAB>0"
      // Третье поле (0/1) в оригинале - что-то вроде вкл/выкл, нам
      // не нужно, просто игнорируем.
      int tab1 = line.indexOf('\t');
      int tab2 = line.indexOf('\t', tab1 + 1);
      String name = line.substring(0, tab1);
      String url = (tab2 != -1) ? line.substring(tab1 + 1, tab2) : line.substring(tab1 + 1);
      name.trim();
      url.trim();
      if (debugLinesLeft > 0) {
        Serial.println("[парсер] tab1=" + String(tab1) + " tab2=" + String(tab2));
        Serial.println("[парсер] name=\"" + name + "\" url=\"" + url + "\"");
        Serial.println("[парсер] url.startsWith(https://) = " + String(url.startsWith("https://")));
      }
      if (url.startsWith("http://") || url.startsWith("https://")) {
        bool ok = addStation(name, url);
        if (debugLinesLeft > 0) {
          Serial.println("[парсер] addStation() вернул: " + String(ok));
        }
        if (ok) added++;
      }
      // Если URL битый (как одна станция в реальном файле пользователя -
      // "://chansonhttps...") - просто пропускаем эту строку, не падаем.
    } else if (line.startsWith("#EXTINF:")) {
      // M3U: "#EXTINF:-1,Имя станции" - имя после первой запятой
      int comma = line.indexOf(',');
      pendingName = (comma != -1) ? line.substring(comma + 1) : "";
      pendingName.trim();
    } else if (line.startsWith("http://") || line.startsWith("https://")) {
      // Голый URL - либо M3U-строка с потоком, либо просто список ссылок
      String name = pendingName.length() > 0 ? pendingName : ("Станция " + String(stationCount + 1));
      if (addStation(name, line)) added++;
      pendingName = "";
    } else if (line.startsWith("Title")) {
      // PLS: "Title1=Имя станции"
      int eq = line.indexOf('=');
      if (eq != -1) {
        pendingName = line.substring(eq + 1);
        pendingName.trim();
      }
    } else if (line.startsWith("File")) {
      // PLS: "File1=http://..."
      int eq = line.indexOf('=');
      if (eq != -1) {
        String url = line.substring(eq + 1);
        url.trim();
        if (url.startsWith("http")) {
          String name = pendingName.length() > 0 ? pendingName : ("Станция " + String(stationCount + 1));
          if (addStation(name, url)) added++;
          pendingName = "";
        }
      }
    }

    if (nl == -1) break;
    pos = nl + 1;
    if (debugLinesLeft > 0) debugLinesLeft--;
  }

  Serial.println("📥 Импортировано станций из плейлиста: " + String(added));
  return added;
}
