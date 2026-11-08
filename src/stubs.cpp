// Заглушки для сборки без радио, будильника и аудио (ESP32_CLASSIC).
#include <Arduino.h>
#include "audio_engine.h"
#include "radio.h"
#include "radio_screen.h"
#include "alarm.h"
#include "hourly_chime.h"

#if defined(ESP32_CLASSIC)

Audio audio;
RadioState radioState;

bool initAudio() { return false; }
void playAudioFile(const String &) {}
void stopAudio() {}
bool isAudioPlaying() { return false; }
void audioLoop() {}
bool connectToStream(const String &) { return false; }
void setAudioVolume(uint8_t) {}
void setAudioEventCallback(AudioEventCallback) {}

void initRadio() {}
bool startRadio(int) { return false; }
void stopRadio() {}
void nextRadioStation() {}
void prevRadioStation() {}
bool isRadioActive() { return false; }
void setRadioVolumePercent(uint8_t) {}
uint8_t getRadioVolumePercent() { return 0; }
void checkRadioSchedule() {}

void initAlarmButton() {}
void checkAlarmButton() {}
void checkAlarms() {}

void checkHourlyChime() {}

void drawRadioScreen() {}

#endif
