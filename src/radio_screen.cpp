#include "radio_screen.h"
#include <math.h>
#include <stdio.h>
#include "globals.h"
#include "fonts.h"
#include "utils.h"
#include "icons.h"
#include "sensors.h"
#include "radio.h"
#include "stations.h"
#include "radio_spectrum.h"

// ============================================================
// ЭКРАН РАДИО (64x32):
//   слева сверху (строки 0..11):  часы (при смене громкости на 2.5 с - "NN%" и полоска громкости)
//   слева ниже (строки 12..21):   спектр (14 столбиков, «пики») ИЛИ VU-метр (два канала L/R), вид выбирается на веб-странице радио
//   справа колонка (одна под другой, выровнены по правому краю):
//        номер станции "N/M"
//        формат потока "MP3"
//        битрейт "128k"
//      нижний край колонки на 3 строки выше бегущей строки, спектр заканчивается за 3 px до колонки
//   низ:                           бегущая строка "станция - трек" (если влезает - по центру)
// Всё подгоняется константами ниже (пометка <-- ПОДВИНУТЬ).
// ============================================================

static const int HDR_H = 12;                    // высота шапки слева
static const int CLOCK_X = 1;                   // часы: левый край                         <-- ПОДВИНУТЬ
static const int COL_W = 20;                    // ширина правой колонки, px (до 5 символов TomThumb)
static const int GAP_TO_COL = 3;                // зазор между спектром и колонкой, px      <-- ПОДВИНУТЬ
static const int GAP_COL_TO_MARQUEE = 3;        // зазор между колонкой и бегущей строкой   <-- ПОДВИНУТЬ
static const int LINE_PITCH = 7;                // шаг строк колонки (текст 5 px + 2 px зазор) <-- ПОДВИНУТЬ
static const int SPEC_TOP = 12;                 // спектр: верхняя строка                   <-- ПОДВИНУТЬ
static const int SPEC_H = 10;                   // спектр: высота (строк)                   <-- ПОДВИНУТЬ
static const int BAR_W = 2;                     // ширина столбика, px
static const int BAR_PITCH = 3;                 // шаг столбиков (14 x 3 = 42)
static const int SCROLL_BOTTOM = 32;            // бегущая строка: нижний край (включая «хвосты» букв) <-- ПОДВИНУТЬ
static const unsigned SCROLL_STEP_MS = 55;      // скорость бегущей строки (меньше = быстрее)
static const unsigned OVERLAY_MS = 2500;        // сколько показывать громкость

// Верх строки по эталонному символу: на сколько ниже cursor-y находится верх букв (зависит от шрифта)
static int fontAscent(const GFXfont *f, const char *ref) {
  display->setFont(f);
  int16_t x1, y1;
  uint16_t w, h;
  display->getTextBounds(ref, 0, 100, &x1, &y1, &w, &h);
  return 100 - y1;
}

static int textWidth(const char *s) {
  int16_t x1, y1;
  uint16_t w, h;
  display->getTextBounds(s, 0, 100, &x1, &y1, &w, &h);
  return w;
}

// Градиент столбика: низ зелёный -> жёлтый -> верх красный
static uint16_t barColor(int row, int rows) {
  float t = rows <= 1 ? 0 : (float)row / (rows - 1);
  int r, g;
  if (t < 0.5f) { r = (int)(510 * t); g = 255; }
  else { r = 255; g = (int)(255 - 510 * (t - 0.5f)); }
  return display->color565(r, g, 0);
}

static void drawSpectrum() {
  for (int b = 0; b < SPEC_BARS; b++) {
    int h = radioSpectrumBar(b, SPEC_H);
    int x = b * BAR_PITCH;
    for (int r = 0; r < h; r++) {
      int y = SPEC_TOP + SPEC_H - 1 - r;
      display->drawFastHLine(x, y, BAR_W, barColor(r, SPEC_H));
    }
    int pk = radioSpectrumPeak(b, SPEC_H);
    if (pk > h && pk > 0) {
      display->drawFastHLine(x, SPEC_TOP + SPEC_H - pk, BAR_W, display->color565(210, 210, 255));
    }
    if (h == 0 && pk == 0) {                       // еле заметная «земля», чтобы экран не казался пустым
      display->drawFastHLine(x, SPEC_TOP + SPEC_H - 1, BAR_W, display->color565(0, 40, 0));
    }
  }
}

// VU-метр: два канала (L сверху, R снизу), сегменты по 2 px, пик одним белым сегментом
static const int VU_X0 = 6;                     // начало полосок (слева буква L/R)        <-- ПОДВИНУТЬ
static const int VU_ROW_L = 13;                 // верхняя строка полоски L                 <-- ПОДВИНУТЬ
static const int VU_ROW_R = 18;                 // верхняя строка полоски R                 <-- ПОДВИНУТЬ
static const int VU_BAR_H = 3;                  // высота полоски, px
static const int VU_SEG_W = 2, VU_SEG_PITCH = 3;
static const int VU_RIGHT = SPEC_BARS * BAR_PITCH - 1;   // правый край (как у спектра, 41)

static void drawVu() {
  const int segs = (VU_RIGHT - VU_X0 + 1 + (VU_SEG_PITCH - VU_SEG_W)) / VU_SEG_PITCH;   // 12
  const int rowsTop[2] = {VU_ROW_L, VU_ROW_R};
  const char *names[2] = {"L", "R"};
  int asc = fontAscent(&TomThumb, "0");
  for (int c = 0; c < 2; c++) {
    display->setFont(&TomThumb);
    display->setTextColor(display->color444(0, 12, 15));
    display->setCursor(0, rowsTop[c] - 1 + asc);          // буква выровнена по полоске
    display->print(names[c]);
    int lit = radioVuLevel(c, segs);
    int pk = radioVuPeak(c, segs);
    for (int i = 0; i < segs; i++) {
      bool on = i < lit;
      uint16_t col;
      if (i >= segs - 2) col = on ? display->color565(255, 30, 0) : display->color565(40, 5, 0);       // красные
      else if (i >= segs - 5) col = on ? display->color565(255, 200, 0) : display->color565(40, 30, 0); // жёлтые
      else col = on ? display->color565(0, 230, 40) : display->color565(0, 30, 6);                      // зелёные
      if (!on && pk > 0 && i == pk - 1) col = display->color565(210, 210, 255);                        // пик
      int x = VU_X0 + i * VU_SEG_PITCH;
      for (int dy = 0; dy < VU_BAR_H; dy++) display->drawFastHLine(x, rowsTop[c] + dy, VU_SEG_W, col);
    }
  }
}

// Верхняя строка бегущей строки (по эталону "Hg") - от неё считается нижний край правой колонки
static int marqueeTopRow() {
  display->setFont(&Heebo7pt8b);
  int16_t x1, y1;
  uint16_t w, h;
  display->getTextBounds("Hg", 0, 100, &x1, &y1, &w, &h);
  return SCROLL_BOTTOM - (int)h;
}

// Одна строка колонки: верх букв на topRow, выравнивание по правому краю экрана
static void drawColLine(const char *txt, int topRow, uint16_t color) {
  if (!txt || !txt[0]) return;
  display->setFont(&TomThumb);
  int asc = fontAscent(&TomThumb, "0");
  display->setTextColor(color);
  display->setCursor(64 - textWidth(txt) + 1, topRow + asc);
  display->print(txt);
}

static void drawInfoColumn() {
  int colBottom = marqueeTopRow() - GAP_COL_TO_MARQUEE - 1;   // последняя занятая строка колонки
  int top3 = colBottom - 4;                                    // верх нижней строки (текст 5 px)
  int pitch = LINE_PITCH;
  if (top3 - 2 * pitch < 0) pitch = top3 / 2;                  // не лезем выше экрана
  int top2 = top3 - pitch, top1 = top2 - pitch;

  char st[28];
  snprintf(st, sizeof(st), "%d/%d", radioState.stationIndex + 1, getStationCount());
  drawColLine(st, top1, display->color444(0, 12, 15));         // номер станции

  drawColLine(radioCodecName(), top2, display->color444(15, 9, 0));   // формат (MP3, AAC...)

  uint32_t kb = radioState.bitrateKbps > 0 ? (uint32_t)radioState.bitrateKbps : radioAudioBitrateKbps();
  if (kb > 0) {
    char br[12];
    snprintf(br, sizeof(br), "%uk", (unsigned)kb);
    drawColLine(br, top3, display->color444(0, 15, 5));        // битрейт
  }
}

static void drawClock() {
  char clk[8];
  snprintf(clk, sizeof(clk), "%d:%02d", hours, minutes);
  int16_t x1, y1;
  uint16_t w, h;
  display->setFont(&Heebo7pt8b);
  display->getTextBounds("0", 0, 100, &x1, &y1, &w, &h);
  int asc = 100 - y1;
  int top = (HDR_H - (int)h) / 2;
  if (top < 0) top = 0;
  display->setTextColor(radioClockColor);
  display->setCursor(CLOCK_X, top + asc);
  display->print(clk);
}

// Громкость: "NN%" и полоска до края спектра
static void drawVolumeOverlay() {
  int vol = getRadioVolumePercent();
  char vb[8];
  snprintf(vb, sizeof(vb), "%d%%", vol);
  int16_t x1, y1;
  uint16_t w, h;
  display->setFont(&Heebo7pt8b);
  display->getTextBounds("0", 0, 100, &x1, &y1, &w, &h);
  int asc = 100 - y1;
  int top = (HDR_H - (int)h) / 2;
  if (top < 0) top = 0;
  display->setTextColor(display->color444(0, 12, 15));
  display->setCursor(CLOCK_X, top + asc);
  display->print(vb);
  int barX = CLOCK_X + textWidth(vb) + 3;
  int barW = SPEC_BARS * BAR_PITCH - 1 - barX;                 // до правого края спектра
  if (barW > 3) {
    int y = HDR_H / 2 - 1;
    display->drawFastHLine(barX, y - 1, barW, display->color565(40, 40, 40));
    display->drawFastHLine(barX, y, barW, display->color565(40, 40, 40));
    display->drawFastHLine(barX, y + 1, barW, display->color565(40, 40, 40));
    int fill = barW * vol / 100;
    for (int dy = -1; dy <= 1; dy++) if (fill > 0) display->drawFastHLine(barX, y + dy, fill, display->color565(0, 200, 255));
  }
}

static void drawMarquee(unsigned long now) {
  display->setFont(&Heebo7pt8b);
  display->setTextColor(radioTextColor);
  display->setTextWrap(false);

  String text;
  if (!radioState.active) text = "Подключение...";
  else if (radioState.trackTitle.length() > 0) text = radioState.stationName + " - " + radioState.trackTitle;
  else text = radioState.stationName;
  String utf8 = utf8rus(text);

  int16_t x1, y1;
  uint16_t w, h;
  display->getTextBounds("Hg", 0, 100, &x1, &y1, &w, &h);
  int cy = 100 + SCROLL_BOTTOM - (y1 + (int)h);      // чтобы низ букв (с хвостами) был на SCROLL_BOTTOM

  static String lastText = "";
  static int textW = 0;
  static int offset = 64;
  static unsigned long lastMove = 0;
  if (text != lastText) {
    lastText = text;
    display->getTextBounds(utf8.c_str(), 0, 100, &x1, &y1, &w, &h);
    textW = w;
    offset = 64;
  }
  if (textW <= 64) {                                  // короткий текст - по центру, без бега
    display->setCursor((64 - textW) / 2 - x1, cy);
    display->print(utf8);
    return;
  }
  if (now - lastMove >= SCROLL_STEP_MS) {
    lastMove = now;
    offset -= 1;
    if (offset < -textW) offset = 64;
  }
  display->setCursor(offset - x1, cy);
  display->print(utf8);
}

void drawRadioScreen() {
  unsigned long now = millis();

  // Смена громкости -> на 2.5 с вместо часов показываем громкость
  static unsigned long overlayUntil = 0;
  static int lastVol = -1;
  int vol = getRadioVolumePercent();
  if (lastVol >= 0 && vol != lastVol) overlayUntil = now + OVERLAY_MS;
  lastVol = vol;

  radioSpectrumUpdate(radioState.active, radioViewMode == 0);

  if (now < overlayUntil) drawVolumeOverlay();
  else drawClock();
  if (radioViewMode == 1) drawVu();
  else drawSpectrum();
  drawInfoColumn();
  drawMarquee(now);
  display->setFont();
}
