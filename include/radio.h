#pragma once
#include <Arduino.h>

// ============================================================
// Веб-радио поверх общего аудио-движка (audio_engine.h/audio.cpp).
// Играть радио и будильник/ночь-утро одновременно нельзя (один DAC,
// один I2S-выход) - ровно как задумано у автора оригинала. Если во
// время радио сработает будильник, playAudioFile() в alarm.cpp сама
// прервёт поток и включит звук будильника - радио просто "выключится"
// (radioState.active станет false на следующем evt_eof/статусе).
// ============================================================

struct RadioState {
  bool active = false;      // сейчас реально играет
  int stationIndex = -1;    // индекс текущей станции в списке, -1 = не выбрана
  String stationName;       // имя станции (из списка или из потока, если прислал evt_name)
  String trackTitle;        // текущий трек из потока (evt_streamtitle), может быть пустым
  int bitrateKbps = 0;
};
extern RadioState radioState;

// Загружает список станций и восстанавливает громкость. Вызывать один
// раз в setup(), ПОСЛЕ initAudio().
void initRadio();

// Запускает станцию по индексу (см. stations.h). false, если индекс
// неверный, список станций пуст, или нет Wi-Fi.
bool startRadio(int stationIndex);

// Останавливает радио, если играло.
void stopRadio();

void nextRadioStation();
void prevRadioStation();

bool isRadioActive();

// 0..100% - переводится в диапазон библиотеки (0..21) внутри.
void setRadioVolumePercent(uint8_t percent);
uint8_t getRadioVolumePercent();

// Проверяет расписание вкл/выкл (аналогично ночному режиму/большим
// часам). Вызывать РАЗ В СЕКУНДУ вместе с checkNightSchedule() и
// checkBigClockSchedule() - НЕ каждый кадр.
void checkRadioSchedule();
