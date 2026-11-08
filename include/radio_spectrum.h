#pragma once
#include <Arduino.h>

// ============================================================
// СПЕКТРОАНАЛИЗАТОР ДЛЯ ЭКРАНА РАДИО (настоящий БПФ по звуку, который играет).
//  - Звук берётся из библиотеки ESP32-audioI2S через её функцию
//    audio_process_raw_samples() (вызывается библиотекой на каждый блок звука,
//    до регулировки громкости - поэтому столбики не зависят от громкости).
//  - Сама функция только копирует отсчёты в маленькое кольцо (быстро, звук не тормозит).
//  - БПФ (512 точек) считается в основном цикле при рисовании экрана, не чаще ~30 раз/с.
// ============================================================

#define SPEC_BARS 14        // столбиков (полос частот, от баса слева до верхов справа)

// Приём блока звука (вызывается из radio_spectrum_hook.cpp)
void radioSpectrumFeed(const int32_t *buff, int validWords);

// Вызывать при каждой отрисовке экрана радио (сама ограничивает частоту расчёта).
void radioSpectrumUpdate(bool playing, bool withSpectrum = true);   // withSpectrum=false: БПФ не считаем (режим VU-метра)

// Высота столбика / «пика» в пикселях, 0..maxH
int radioSpectrumBar(int i, int maxH);
int radioSpectrumPeak(int i, int maxH);

// VU-метр: уровень канала (0 - левый, 1 - правый) в сегментах 0..segs и «пик»
int radioVuLevel(int ch, int segs);
int radioVuPeak(int ch, int segs);

// Получает ли спектр звук (для диагностики: false = библиотека не вызывает перехват)
bool radioSpectrumHasSignal();

// Для проверки на компьютере: 512 отсчётов (моно, int16) -> уровни полос 0..1 (без сглаживания)
void radioSpectrumAnalyze(const int16_t *samples512, uint32_t sampleRate, float *levelOut);

// Сведения о потоке из библиотеки (для колонки справа на экране радио)
const char *radioCodecName();      // "MP3", "AAC", "FLAC"... или "" если ещё не определён
uint32_t radioAudioBitrateKbps();  // битрейт по данным библиотеки, кбит/с (0 если неизвестен)
