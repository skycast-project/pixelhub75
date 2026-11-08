#include "alarm.h"
#include "globals.h"
#include "audio_engine.h"
#include "pins.h"
#include <SD.h>

// Папка с мелодиями будильника на SD-карте - прописана жёстко.
// Файлы называются по номеру: 000.mp3, 001.mp3 ... 999.mp3.
// В веб-интерфейсе указывается только номер трека.
static const char *ALARM_FOLDER = "/alarms";

// День недели, для которого сейчас звонит будильник (чтобы при переходе
// через полночь не подхватить звук другого дня при повторном запуске).
static int alarmDay = 0;

// Перевод процентов (0..100) в шкалу аудио-движка (0..21).
static uint8_t volumeFromPercent(uint8_t percent) {
  if (percent > 100) percent = 100;
  return (uint8_t)((percent * 21 + 50) / 100);
}

// Достаёт номер трека из сохранённого значения. Понимает "5", "005",
// а также старый формат "/alarms/005.mp3". Если это не номер - вернёт -1.
static int parseTrackNumber(const String &stored) {
  String s = stored;
  s.trim();
  int slash = s.lastIndexOf('/');
  if (slash >= 0) s = s.substring(slash + 1);
  int dot = s.indexOf('.');
  if (dot >= 0) s = s.substring(0, dot);
  if (s.length() == 0 || s.length() > 3) return -1;
  for (unsigned int i = 0; i < s.length(); i++) {
    if (!isdigit((unsigned char)s[i])) return -1;
  }
  return s.toInt();
}

// Полный путь к файлу будильника для указанного дня: /alarms/NNN.mp3
static String alarmPathFor(int day) {
  int n = parseTrackNumber(alarmSoundPath[day]);
  if (n < 0) {
    Serial.printf("⚠️ Будильник: в настройках не номер трека ('%s'), беру трек 1\n",
                  String(alarmSoundPath[day]).c_str());
    n = 1;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%s/%03d.mp3", ALARM_FOLDER, n);
  return String(buf);
}

// Запускает звук будильника с его громкостью. Если файла нет - пишет
// ОДНУ ошибку в лог и возвращает false (повторных попыток не делаем,
// чтобы не засорять лог).
static bool startAlarmSound(int day) {
  String path = alarmPathFor(day);
  if (!SD.exists(path)) {
    Serial.println("❌ Будильник: файл не найден на SD: " + path);
    return false;
  }
  setAudioVolume(volumeFromPercent(alarmVolumePercent));
  playAudioFile(path);
  return true;
}

// Завершает будильник и возвращает громкость радио (аудио-движок общий,
// иначе радио/озвучка могли бы остаться на громкости будильника).
static void finishAlarm() {
  stopAudio();
  alarmActive = false;
  setAudioVolume(volumeFromPercent(radioVolumePercent));
}

void initAlarmButton() {
  pinMode(ALARM_BUTTON_PIN, INPUT_PULLUP);
}

void checkAlarmButton() {
  static bool lastState = HIGH;          // не нажата (подтянута к VCC)
  static unsigned long lastChangeAt = 0;

  bool state = digitalRead(ALARM_BUTTON_PIN);
  if (state != lastState) {
    lastState = state;
    lastChangeAt = millis();
  }

  // Дебаунс 50мс - реагируем, только если состояние устойчиво держится
  bool pressed = (state == LOW) && (millis() - lastChangeAt > 50);

  if (pressed && alarmActive) {
    finishAlarm();
    Serial.println("🔇 Будильник остановлен кнопкой");
  }
}

// Срабатывает один раз в момент совпадения часа/минуты/секунды==0.
// static lastTriggeredMinute защищает от повторного запуска в течение
// той же минуты, даже если checkAlarms() вызовут чуть чаще раза в секунду.
void checkAlarms() {
  static int lastTriggeredMinute = -1;

  if (!alarmActive) {
    // Ищем, не пора ли начать будильник сегодня
    int today = wdayNum; // 0..6, как tm_wday
    if (alarmEnabled[today] &&
        hours == alarmHour[today] &&
        minutes == alarmMinute[today] &&
        lastTriggeredMinute != minutes) {
      lastTriggeredMinute = minutes;
      Serial.println("⏰ Будильник сработал (день " + String(today) + ")");
      if (startAlarmSound(today)) {
        alarmDay = today;
        alarmActive = true;
        alarmStartedAt = millis();
      }
    }
  } else {
    // Уже играет - следим за автостопом
    unsigned long elapsedMs = millis() - alarmStartedAt;
    unsigned long limitMs = (unsigned long)alarmAutoStopMinutes * 60000UL;

    if (elapsedMs >= limitMs) {
      finishAlarm();
      Serial.println("🔇 Будильник: автостоп по таймауту");
    } else if (!isAudioPlaying()) {
      // Файл доиграл сам (короткий сигнал или песня), но время автостопа
      // ещё не вышло - запускаем заново по кругу, чтобы будильник
      // продолжал звучать, а не замолкал после первого проигрывания
      if (!startAlarmSound(alarmDay)) {
        finishAlarm();
      }
    }
  }
}
