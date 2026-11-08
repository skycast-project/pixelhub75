// Перехват звука из ESP32-audioI2S для спектроанализатора.
// Библиотека объявляет эту функцию как «слабую» (weak) и вызывает на каждый блок звука,
// наша версия её заменяет. Файл намеренно НЕ подключает Audio.h (чтобы определение не стало «слабым»).
// Сигнатура должна совпадать с Audio.h:  void audio_process_raw_samples(int32_t* outBuff, int16_t validSamples);
#include <Arduino.h>
#include "radio_spectrum.h"

void audio_process_raw_samples(int32_t *outBuff, int16_t validSamples) {
  radioSpectrumFeed(outBuff, validSamples);
}
