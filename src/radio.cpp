#include "radio.h"
#include "stations.h"
#include "audio_engine.h"
#include "globals.h"
#include "storage.h"
#include <WiFi.h>

RadioState radioState;

// --- коллбэк аудио-событий: обновляем radioState по данным из потока ---
static void onRadioAudioEvent(Audio::msg_t m) {
  switch (m.e) {
    case Audio::evt_name:
      radioState.stationName = String(m.msg);
      break;
    case Audio::evt_streamtitle:
      radioState.trackTitle = String(m.msg);
      break;
    case Audio::evt_bitrate:
      radioState.bitrateKbps = String(m.msg).toInt() / 1000;
      break;
    case Audio::evt_eof:
      // Поток оборвался (сервер закрыл соединение и т.п.) - просто
      // сбрасываем состояние. Автопереподключение не делаем сознательно -
      // пользователь сам решает через веб, чтобы не заспамить станцию
      // бесконечными реконнектами при плохой сети.
      radioState.active = false;
      break;
    default:
      break;
  }
}

void initRadio() {
  loadStations();
  setAudioEventCallback(onRadioAudioEvent);
  setAudioVolume((uint8_t)map(radioVolumePercent, 0, 100, 0, 21));
}

bool startRadio(int stationIndex) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Радио: нет Wi-Fi");
    return false;
  }
  StationInfo st = getStation(stationIndex);
  if (st.url.length() == 0) {
    Serial.println("❌ Радио: станция не найдена по индексу " + String(stationIndex));
    return false;
  }

  radioState.stationIndex = stationIndex;
  radioState.stationName = st.name; // сразу показываем имя из списка -
                                     // поток может потом прислать своё (evt_name), перезапишется
  radioState.trackTitle = "";
  radioState.bitrateKbps = 0;

  if (connectToStream(st.url)) {
    radioState.active = true;
    radioLastStationIndex = stationIndex;
    saveConfig(); // запоминаем станцию на случай перезагрузки
    Serial.println("📻 Играю станцию: " + st.name);
    return true;
  }

  radioState.active = false;
  return false;
}

void stopRadio() {
  stopAudio();
  radioState.active = false;
  radioState.trackTitle = "";
}

void nextRadioStation() {
  int count = getStationCount();
  if (count == 0) return;
  int next = (radioState.stationIndex + 1) % count;
  startRadio(next);
}

void prevRadioStation() {
  int count = getStationCount();
  if (count == 0) return;
  int prev = (radioState.stationIndex - 1 + count) % count;
  startRadio(prev);
}

bool isRadioActive() {
  return radioState.active;
}

void setRadioVolumePercent(uint8_t percent) {
  if (percent > 100) percent = 100;
  radioVolumePercent = percent;
  saveConfig();
  setAudioVolume((uint8_t)map(percent, 0, 100, 0, 21));
}

uint8_t getRadioVolumePercent() {
  return radioVolumePercent;
}

void checkRadioSchedule() {
  if (!radioScheduleEnabled) return;

  bool insideWindow;
  if (radioOnHour == radioOffHour && radioOnMinute == radioOffMinute) {
    insideWindow = false; // окно нулевой длины - считаем выключенным
  } else if (radioOnHour < radioOffHour ||
             (radioOnHour == radioOffHour && radioOnMinute < radioOffMinute)) {
    // окно НЕ через полночь, напр. 8:00..22:00
    bool afterOn = (hours > radioOnHour) || (hours == radioOnHour && minutes >= radioOnMinute);
    bool beforeOff = (hours < radioOffHour) || (hours == radioOffHour && minutes < radioOffMinute);
    insideWindow = afterOn && beforeOff;
  } else {
    // окно ЧЕРЕЗ полночь
    bool afterOn = (hours > radioOnHour) || (hours == radioOnHour && minutes >= radioOnMinute);
    bool beforeOff = (hours < radioOffHour) || (hours == radioOffHour && minutes < radioOffMinute);
    insideWindow = afterOn || beforeOff;
  }

  static bool wasInsideWindow = false;
  if (insideWindow && !wasInsideWindow) {
    // Наступило окно расписания - включаем радио на последней станции
    int idx = (radioLastStationIndex >= 0) ? radioLastStationIndex : 0;
    startRadio(idx);
  } else if (!insideWindow && wasInsideWindow) {
    stopRadio();
  }
  wasInsideWindow = insideWindow;
}
