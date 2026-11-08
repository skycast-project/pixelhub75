#include "alarm_df.h"
#include "globals.h"
#include "dfplayer.h"

static int s_lastMinute = -1;

void alarmDfpInit() { s_lastMinute = -1; }

bool alarmDfpActive() { return alarmActive; }

void alarmDfpStop() {
  if (!alarmActive) return;
  dfpStop();
  alarmActive = false;
  Serial.println("🔇 Будильник остановлен");
}

void alarmDfpTick() {
  if (alarmActive) {
    unsigned long limit = (unsigned long)alarmAutoStopMinutes * 60000UL;
    if (alarmAutoStopMinutes > 0 && millis() - alarmStartedAt >= limit) {
      Serial.println("🔇 Будильник: автостоп");
      alarmDfpStop();
    }
    return;
  }
  int today = wdayNum;                 // 0 = воскресенье
  if (today < 0 || today > 6) return;
  if (!alarmEnabled[today]) return;
  if (hours != alarmHour[today] || minutes != alarmMinute[today]) return;
  if (s_lastMinute == minutes) return;
  s_lastMinute = minutes;

  int track = 1;
  String s = alarmSoundPath[today];
  s.trim();
  int slash = s.lastIndexOf('/');
  if (slash >= 0) s = s.substring(slash + 1);
  int dot = s.indexOf('.');
  if (dot >= 0) s = s.substring(0, dot);
  if (s.length() && s.toInt() > 0) track = constrain(s.toInt(), 1, 9);

  Serial.printf("⏰ Будильник: день %d, трек %d\n", today, track);
  if (dfpReady()) {
    dfpPlayFolder(2, (uint8_t)track);
    alarmActive = true;
    alarmStartedAt = millis();
  }
}
