#include "dfplayer.h"
#include "globals.h"
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>



static HardwareSerial dfpSerial(2);
static DFRobotDFPlayerMini dfp;
static bool s_ok = false;
static uint8_t s_vol = 20;
static int s_tracks = 0;

void dfpInit() {
  dfpSerial.begin(9600, SERIAL_8N1, dfpRxPin, dfpTxPin);
  if (!dfp.begin(dfpSerial, false, false)) {
    Serial.printf("ℹ️ DFPlayer не найден (TX %d, RX %d)\n", dfpTxPin, dfpRxPin);
    return;
  }
  s_ok = true;
  dfp.setTimeOut(500);
  s_vol = (uint8_t)constrain(dfVolume, 0, 30);
  dfp.volume(s_vol);
  s_tracks = dfp.readFileCounts();
  Serial.printf("✅ DFPlayer найден, файлов на карте: %d\n", s_tracks);
}

bool dfpReady() { return s_ok; }
uint8_t dfpVolume() { return s_vol; }

void dfpSetVolume(uint8_t vol) {
  s_vol = vol > 30 ? 30 : vol;
  dfVolume = s_vol;
  if (s_ok) dfp.volume(s_vol);
}

bool dfpPlayTrack(uint16_t n) {
  if (!s_ok) return false;
  dfp.play(n);
  return true;
}

bool dfpPlayFolder(uint8_t folder, uint8_t file) {
  if (!s_ok) return false;
  dfp.playFolder(folder, file);
  return true;
}

void dfpStop() { if (s_ok) dfp.stop(); }
int dfpTracks() { return s_tracks; }
