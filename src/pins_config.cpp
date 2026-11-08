#include "pins_config.h"
#include <Preferences.h>

// ============================================================
//  Хранение пинов матрицы в NVS (переживает обновление прошивки
//  и заливку файловой системы).
// ============================================================

static const char *NVS_NS  = "pins";
static const char *NVS_KEY = "matrix";
static const uint8_t PINS_VERSION = 1;

MatrixPins pinsDefaults() {
  MatrixPins p;
  p.r1  = R1_PIN;  p.g1 = G1_PIN;  p.b1 = B1_PIN;
  p.r2  = R2_PIN;  p.g2 = G2_PIN;  p.b2 = B2_PIN;
  p.a   = A_PIN;   p.b  = B_PIN;   p.c  = C_PIN;
  p.d   = D_PIN;   p.e  = E_PIN;
  p.lat = LAT_PIN; p.oe = OE_PIN;  p.clk = CLK_PIN;
  return p;
}

// Запись: [версия][14 пинов]
struct PinsBlob {
  uint8_t version;
  MatrixPins pins;
} __attribute__((packed));

MatrixPins pinsLoad() {
  Preferences prefs;
  MatrixPins def = pinsDefaults();
  if (!prefs.begin(NVS_NS, true)) return def;
  PinsBlob blob;
  size_t n = prefs.getBytesLength(NVS_KEY);
  bool ok = (n == sizeof(blob)) && (prefs.getBytes(NVS_KEY, &blob, sizeof(blob)) == sizeof(blob));
  prefs.end();
  if (!ok || blob.version != PINS_VERSION) return def;
  String err;
  if (!pinsValidate(blob.pins, err)) return def;
  return blob.pins;
}

bool pinsSave(const MatrixPins &p) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) return false;
  PinsBlob blob{PINS_VERSION, p};
  size_t n = prefs.putBytes(NVS_KEY, &blob, sizeof(blob));
  prefs.end();
  return n == sizeof(blob);
}

void pinsReset() {
  Preferences prefs;
  if (prefs.begin(NVS_NS, false)) {
    prefs.remove(NVS_KEY);
    prefs.end();
  }
}

static bool pinAllowed(int v) {
  if (v == -1) return true;                       // не используется
  if (v < 0 || v > 48) return false;
  if (v >= 6 && v <= 11) return false;            // заняты флеш-памятью
  if (v >= 34 && v <= 39) return false;           // только вход
  return true;
}

bool pinsValidate(const MatrixPins &p, String &error) {
  const int v[14] = { p.r1, p.g1, p.b1, p.r2, p.g2, p.b2,
                      p.a, p.b, p.c, p.d, p.e, p.lat, p.oe, p.clk };
  const char *names[14] = { "R1", "G1", "B1", "R2", "G2", "B2",
                            "A", "B", "C", "D", "E", "LAT", "OE", "CLK" };
  for (int i = 0; i < 14; i++) {
    if (i == 10 && v[i] == -1) continue;          // E можно не использовать
    if (!pinAllowed(v[i])) {
      error = String(names[i]) + ": недопустимый GPIO " + String(v[i]) +
              " (нельзя 6-11 и 34-39, допустимо 0-5, 12-33, 40-48)";
      return false;
    }
    if (i != 10 && v[i] == -1) {
      error = String(names[i]) + ": пин обязателен";
      return false;
    }
  }
  for (int i = 0; i < 14; i++) {
    if (v[i] == -1) continue;
    for (int j = i + 1; j < 14; j++) {
      if (v[j] == v[i]) {
        error = String("пин ") + String(v[i]) + " назначен дважды (" +
                names[i] + " и " + names[j] + ")";
        return false;
      }
    }
  }
  error = "";
  return true;
}
