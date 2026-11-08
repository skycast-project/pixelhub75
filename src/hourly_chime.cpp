#include "hourly_chime.h"
#include "globals.h"
#include "audio_engine.h"
#include "night_screen.h"

// Номер папки на SD для каждого режима (та же схема, что в оригинале
// Шилова - файлы внутри называются 000.mp3.../023.mp3, где номер файла
// это час). Порядок ДОЛЖЕН совпадать с порядком опций в веб-интерфейсе
// (clock.html, select "hourlyVoice") - индекс массива == hourlyVoiceMode.
static const char* HOURLY_FOLDERS[4] = {
  "22",  // 0 - женский
  "23",  // 1 - мужской
  "12",  // 2 - куранты
  "13"   // 3 - каминные часы
};

// Проценты (0..100) -> громкость библиотеки (0..21)
static uint8_t percentToVolume(uint8_t p) {
  if (p > 100) p = 100;
  return (uint8_t)((p * 21 + 50) / 100);
}

// Озвучка играет с СВОЕЙ громкостью (hourlyVolumePercent). Когда файл
// доиграл - возвращаем громкость радио, как это делает будильник.
static bool chimePlaying = false;
static unsigned long chimeStartedAt = 0;

void checkHourlyChime() {
  // Возврат громкости после озвучки. Первые 3 секунды не проверяем:
  // библиотека может не сразу сообщить, что файл начал играть.
  if (chimePlaying && millis() - chimeStartedAt > 3000 && !isAudioPlaying()) {
    chimePlaying = false;
    setAudioVolume(percentToVolume(radioVolumePercent));
  }

  if (!hourlyAnnounceEnabled) return;
  if (isNightModeActive()) return;  // как и в оригинале - ночью не озвучиваем
  if (alarmActive) return;          // не перебиваем звонящий будильник

  // Срабатывает один раз в момент 00:00 каждого часа - lastHour
  // защищает от повторного срабатывания в течение той же минуты
  static int lastHour = -1;
  if (minutes != 0 || seconds != 0) return;
  if (hours == lastHour) return;
  lastHour = hours;

  int mode = hourlyVoiceMode;
  if (mode < 0 || mode > 3) mode = 0;

  char path[24];
  snprintf(path, sizeof(path), "/%s/%03d.mp3", HOURLY_FOLDERS[mode], hours);
  uint8_t vol = percentToVolume(hourlyVolumePercent);
  setAudioVolume(vol);               // до старта файла
  playAudioFile(String(path));
  setAudioVolume(vol);               // и сразу после - на случай, если запуск сбрасывает громкость
  chimePlaying = true;
  chimeStartedAt = millis();
  Serial.println("🔔 Почасовая озвучка: " + String(path) + ", громкость " + String(hourlyVolumePercent) + "%");
}
