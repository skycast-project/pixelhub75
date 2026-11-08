#include "radio_spectrum.h"
#include <math.h>
#include <string.h>
#ifndef SPECTRUM_HOST_TEST
#include "audio_engine.h"      // подключает Audio.h (объявление audio_process_raw_samples и класс Audio)
extern Audio audio;            // создан в audio_engine.cpp
#endif

// ---------- НАСТРОЙКИ (при желании подкрутить) ----------
static const int   FFT_N = 512;            // точек БПФ (при 44.1 кГц шаг 86 Гц)
static const float FREQ_LOW = 90.0f;       // левая граница первой полосы, Гц
static const float FREQ_HIGH = 12000.0f;   // правая граница последней полосы, Гц
static const float DB_FLOOR = -68.0f;      // уровень, который считается «тишиной» (дБ от полной шкалы)
static const float DB_RANGE = 48.0f;       // сколько дБ занимает вся высота столбика
static const float TILT_DB = 0.5f;         // подъём верхов (у музыки они тише), дБ на полосу
static const float DYN_RANGE_DB = 28.0f;   // сколько дБ ниже самой громкой полосы ещё видно (меньше = резче, больше динамики)
static const float REF_HEADROOM_DB = 2.0f; // запас над опорным уровнем
static const float REF_FALL_DB_PER_SEC = 4.0f; // как быстро опорный уровень «опускается» после громкого места
static const float REF_MIN_DB = -48.0f;    // ниже этого опорный уровень не опускаем (тихий шум не растягиваем)
static const float FALL_PER_SEC = 2.2f;    // скорость падения столбика (долей высоты в секунду)
static const float PEAK_FALL_PER_SEC = 1.1f;
static const unsigned PEAK_HOLD_MS = 280;  // сколько «пик» висит перед падением
static const unsigned FRAME_MS = 33;       // расчёт не чаще раза в 33 мс (~30 к/с)

// ---------- Приём отсчётов (вызывается из звуковой задачи библиотеки) ----------
static volatile int16_t ring[FFT_N];
static volatile uint16_t ringPos = 0;
static volatile uint32_t lastFeedMs = 0;
static volatile uint32_t feedCount = 0;
// накопители для VU-метра (по каналам): сумма квадратов, число отсчётов
static volatile float vuSumSq[2] = {0, 0};
static volatile uint32_t vuCount = 0;

// Вызывается из radio_spectrum_hook.cpp. Данные: пары L,R по 32 бита (звук в старших 16 битах).
void radioSpectrumFeed(const int32_t *buff, int validWords) {
  uint16_t p = ringPos;
  float sl = 0, sr = 0;
  int n = 0;
  for (int i = 0; i + 1 < validWords; i += 2) {
    int32_t l = buff[i] >> 16, r = buff[i + 1] >> 16;               // 16-битные отсчёты каналов
    ring[p] = (int16_t)((l + r) / 2);                                // моно для спектра
    sl += (float)l * l; sr += (float)r * r; n++;
    p = (p + 1) & (FFT_N - 1);
  }
  vuSumSq[0] = vuSumSq[0] + sl;
  vuSumSq[1] = vuSumSq[1] + sr;
  vuCount = vuCount + n;
  ringPos = p;
  feedCount = feedCount + 1;
  lastFeedMs = millis();
}
#ifdef SPECTRUM_HOST_TEST
void spectrumHostFeed(const int16_t *s, int n) {
  uint16_t p = ringPos;
  float sq = 0;
  for (int i = 0; i < n; i++) { ring[p] = s[i]; sq += (float)s[i] * s[i]; p = (p + 1) & (FFT_N - 1); }
  vuSumSq[0] = vuSumSq[0] + sq; vuSumSq[1] = vuSumSq[1] + sq * 0.25f;   // в тесте правый канал тише на 6 дБ
  vuCount = vuCount + n;
  ringPos = p;
  feedCount = feedCount + 1;
  lastFeedMs = millis();
}
#endif

// ---------- БПФ ----------
static float reBuf[FFT_N], imBuf[FFT_N];
static float winTab[FFT_N];
static float cosTab[FFT_N / 2], sinTab[FFT_N / 2];
static uint16_t revTab[FFT_N];
static bool tablesReady = false;

static void initTables() {
  if (tablesReady) return;
  for (int i = 0; i < FFT_N; i++) winTab[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (FFT_N - 1)));
  for (int i = 0; i < FFT_N / 2; i++) {
    cosTab[i] = cosf(2.0f * (float)M_PI * i / FFT_N);
    sinTab[i] = -sinf(2.0f * (float)M_PI * i / FFT_N);
  }
  int bits = 0;
  while ((1 << bits) < FFT_N) bits++;
  for (int i = 0; i < FFT_N; i++) {
    int r = 0;
    for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
    revTab[i] = (uint16_t)r;
  }
  tablesReady = true;
}

static void fft() {
  for (int i = 0; i < FFT_N; i++) {
    int j = revTab[i];
    if (j > i) {
      float t = reBuf[i]; reBuf[i] = reBuf[j]; reBuf[j] = t;
      t = imBuf[i]; imBuf[i] = imBuf[j]; imBuf[j] = t;
    }
  }
  for (int len = 2; len <= FFT_N; len <<= 1) {
    int half = len >> 1, step = FFT_N / len;
    for (int i = 0; i < FFT_N; i += len) {
      for (int k = 0; k < half; k++) {
        float wr = cosTab[k * step], wi = sinTab[k * step];
        int a = i + k, b = a + half;
        float tr = reBuf[b] * wr - imBuf[b] * wi;
        float ti = reBuf[b] * wi + imBuf[b] * wr;
        reBuf[b] = reBuf[a] - tr; imBuf[b] = imBuf[a] - ti;
        reBuf[a] += tr; imBuf[a] += ti;
      }
    }
  }
}

// Границы полос в номерах отсчётов БПФ (логарифмическая шкала, без пересечений)
static void bandEdges(uint32_t fs, int *lo, int *hi) {
  float binW = (float)fs / FFT_N;
  float fHigh = FREQ_HIGH;
  if (fHigh > fs * 0.45f) fHigh = fs * 0.45f;           // не выше предела дискретизации
  int prevHi = 0;                                        // 0-й отсчёт (постоянная составляющая) не берём
  for (int b = 0; b < SPEC_BARS; b++) {
    float f0 = FREQ_LOW * powf(fHigh / FREQ_LOW, (float)b / SPEC_BARS);
    float f1 = FREQ_LOW * powf(fHigh / FREQ_LOW, (float)(b + 1) / SPEC_BARS);
    int l = (int)floorf(f0 / binW + 0.5f);
    int h = (int)floorf(f1 / binW + 0.5f) - 1;
    if (l <= prevHi) l = prevHi + 1;
    if (h < l) h = l;
    if (h > FFT_N / 2 - 1) h = FFT_N / 2 - 1;
    if (l > h) l = h;
    lo[b] = l; hi[b] = h;
    prevHi = h;
  }
}

// Уровни полос в дБ (от полной шкалы), с небольшим подъёмом верхов
static void analyzeDb(const int16_t *s, uint32_t fs, float *outDb) {
  initTables();
  if (fs < 8000 || fs > 192000) fs = 44100;
  for (int i = 0; i < FFT_N; i++) { reBuf[i] = (float)s[i] * winTab[i]; imBuf[i] = 0; }
  fft();
  int lo[SPEC_BARS], hi[SPEC_BARS];
  bandEdges(fs, lo, hi);
  const float norm = 32768.0f * FFT_N * 0.25f;          // синус полной шкалы даёт 0 дБ
  for (int b = 0; b < SPEC_BARS; b++) {
    float m = 0;
    for (int k = lo[b]; k <= hi[b]; k++) {
      float mag = sqrtf(reBuf[k] * reBuf[k] + imBuf[k] * imBuf[k]);
      if (mag > m) m = mag;
    }
    outDb[b] = 20.0f * log10f(m / norm + 1e-9f) + TILT_DB * b;
  }
}

// Для проверки: фиксированная шкала 0..1 (без автоподстройки)
void radioSpectrumAnalyze(const int16_t *s, uint32_t fs, float *out) {
  float db[SPEC_BARS];
  analyzeDb(s, fs, db);
  for (int b = 0; b < SPEC_BARS; b++) {
    float lv = (db[b] + 68.0f) / 48.0f;
    out[b] = lv < 0 ? 0 : (lv > 1 ? 1 : lv);
  }
}

// ---------- Сглаживание и «пики» ----------
static float barV[SPEC_BARS], peakV[SPEC_BARS];
static uint32_t peakHoldUntil[SPEC_BARS];
static uint32_t lastFrameMs = 0;
static float vuV[2], vuPeakV[2];
static uint32_t vuPeakHoldUntil[2];
static const float VU_DB_FLOOR = -46.0f;       // уровень «ноль» шкалы VU, дБ от полной шкалы
static const float VU_DB_RANGE = 43.0f;        // сколько дБ занимает вся шкала
static const float VU_FALL_PER_SEC = 1.6f;
static const float VU_PEAK_FALL_PER_SEC = 0.8f;
static const unsigned VU_PEAK_HOLD_MS = 600;
static float refDb = -30.0f;                            // опорный уровень (дБ), подстраивается под громкость потока

bool radioSpectrumHasSignal() {
  return feedCount > 0 && (millis() - lastFeedMs) < 500;
}

void radioSpectrumUpdate(bool playing, bool withSpectrum) {
  uint32_t now = millis();
  if (lastFrameMs != 0 && now - lastFrameMs < FRAME_MS) return;
  float dt = lastFrameMs == 0 ? 0.033f : (now - lastFrameMs) / 1000.0f;
  if (dt > 0.2f) dt = 0.2f;
  lastFrameMs = now;

  float lvl[SPEC_BARS];
  bool live = playing && radioSpectrumHasSignal();

  // --- VU-метр: среднеквадратичный уровень каналов за прошедшее время ---
  {
    float sq0 = vuSumSq[0], sq1 = vuSumSq[1];
    uint32_t cnt = vuCount;
    vuSumSq[0] = 0; vuSumSq[1] = 0; vuCount = 0;
    float target[2] = {0, 0};
    if (live && cnt > 0) {
      float sq[2] = {sq0, sq1};
      for (int c = 0; c < 2; c++) {
        float rms = sqrtf(sq[c] / cnt) / 32768.0f;
        float db = 20.0f * log10f(rms + 1e-9f);
        float v = (db - VU_DB_FLOOR) / VU_DB_RANGE;
        target[c] = v < 0 ? 0 : (v > 1 ? 1 : v);
      }
    }
    for (int c = 0; c < 2; c++) {
      if (target[c] >= vuV[c]) vuV[c] += (target[c] - vuV[c]) * 0.7f;
      else { vuV[c] -= VU_FALL_PER_SEC * dt; if (vuV[c] < target[c]) vuV[c] = target[c]; }
      if (vuV[c] < 0) vuV[c] = 0;
      if (vuV[c] >= vuPeakV[c]) { vuPeakV[c] = vuV[c]; vuPeakHoldUntil[c] = now + VU_PEAK_HOLD_MS; }
      else if (now >= vuPeakHoldUntil[c]) { vuPeakV[c] -= VU_PEAK_FALL_PER_SEC * dt; if (vuPeakV[c] < vuV[c]) vuPeakV[c] = vuV[c]; }
    }
  }
  if (!withSpectrum) return;                              // режим VU-метра: БПФ не нужен

  if (live) {
    int16_t snap[FFT_N];
    uint16_t p = ringPos;
    for (int i = 0; i < FFT_N; i++) snap[i] = ring[(p + i) & (FFT_N - 1)];   // самые свежие 512 отсчётов по порядку
    uint32_t fs = 44100;
#ifndef SPECTRUM_HOST_TEST
    fs = audio.getSampleRate();
#endif
    float db[SPEC_BARS];
    analyzeDb(snap, fs, db);
    // Опорный уровень = самая громкая полоса: быстро вверх, медленно вниз.
    // Столбики показывают, насколько полоса ниже опорного (так видны и биты, и тихие места).
    float mx = -120.0f;
    for (int b = 0; b < SPEC_BARS; b++) if (db[b] > mx) mx = db[b];
    if (mx > refDb) refDb += (mx - refDb) * 0.5f;
    else refDb -= REF_FALL_DB_PER_SEC * dt;
    if (refDb < REF_MIN_DB) refDb = REF_MIN_DB;
    float top = refDb + REF_HEADROOM_DB;
    for (int b = 0; b < SPEC_BARS; b++) {
      float v = (db[b] - (top - DYN_RANGE_DB)) / DYN_RANGE_DB;
      lvl[b] = v < 0 ? 0 : (v > 1 ? 1 : v);
    }
  } else {
    for (int b = 0; b < SPEC_BARS; b++) lvl[b] = 0;
  }

  for (int b = 0; b < SPEC_BARS; b++) {
    if (lvl[b] >= barV[b]) barV[b] += (lvl[b] - barV[b]) * 0.75f;       // быстро вверх
    else { barV[b] -= FALL_PER_SEC * dt; if (barV[b] < lvl[b]) barV[b] = lvl[b]; }  // плавно вниз
    if (barV[b] < 0) barV[b] = 0;
    if (barV[b] >= peakV[b]) { peakV[b] = barV[b]; peakHoldUntil[b] = now + PEAK_HOLD_MS; }
    else if (now >= peakHoldUntil[b]) { peakV[b] -= PEAK_FALL_PER_SEC * dt; if (peakV[b] < barV[b]) peakV[b] = barV[b]; }
  }
}

int radioVuLevel(int ch, int segs) {
  if (ch < 0 || ch > 1) return 0;
  int n = (int)lroundf(vuV[ch] * segs);
  return n < 0 ? 0 : (n > segs ? segs : n);
}

int radioVuPeak(int ch, int segs) {
  if (ch < 0 || ch > 1) return 0;
  int n = (int)lroundf(vuPeakV[ch] * segs);
  return n < 0 ? 0 : (n > segs ? segs : n);
}

int radioSpectrumBar(int i, int maxH) {
  if (i < 0 || i >= SPEC_BARS) return 0;
  int h = (int)lroundf(barV[i] * maxH);
  return h < 0 ? 0 : (h > maxH ? maxH : h);
}

int radioSpectrumPeak(int i, int maxH) {
  if (i < 0 || i >= SPEC_BARS) return 0;
  int h = (int)lroundf(peakV[i] * maxH);
  return h < 0 ? 0 : (h > maxH ? maxH : h);
}

// ---------- Сведения о потоке ----------
#ifndef SPECTRUM_HOST_TEST
const char *radioCodecName() {
  int c = audio.getCodec();
  if (c <= 0) return "";                  // 0 = «unknown»
  return audio.getCodecname();
}
uint32_t radioAudioBitrateKbps() {
  return audio.getBitRate() / 1000;
}
#else
const char *radioCodecName() { return "MP3"; }
uint32_t radioAudioBitrateKbps() { return 128; }
#endif
