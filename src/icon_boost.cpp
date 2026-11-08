#include "icon_boost.h"
#include <math.h>
#include "globals.h"

// ---------- НАСТРОЙКИ ЯРКОСТИ ИКОНОК ----------
// Увеличить яркость - поднять ICON_GAIN (1.0 = как было; 2.0 - ярко; 2.6 - очень ярко)
// и/или уменьшить ICON_GAMMA (1.0 = без гаммы; 0.65 - заметно подтягивает тёмные цвета).   <-- ПОДОБРАТЬ
static const float ICON_GAIN = 2.2f;
static const float ICON_GAMMA = 0.8f;

static uint8_t gammaLut[256];
static bool lutReady = false;

static void initLut() {
  for (int i = 0; i < 256; i++) gammaLut[i] = (uint8_t)lroundf(255.0f * powf(i / 255.0f, ICON_GAMMA));
  lutReady = true;
}

void drawRGBBitmapBoostGain(int x, int y, const uint16_t *bmp, int w, int h, float gain) {
  static uint16_t buf[256];                    // до 16x16
  if (w * h > 256) { display->drawRGBBitmap(x, y, bmp, w, h); return; }
  if (!lutReady) initLut();
  for (int i = 0; i < w * h; i++) {
    uint16_t c = bmp[i];
    int r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
    if (c == 0) { buf[i] = 0; continue; }
    float r = (r5 * 255 + 15) / 31, g = (g6 * 255 + 31) / 63, b = (b5 * 255 + 15) / 31;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (mx <= 0) { buf[i] = 0; continue; }
    // новая яркость = гамма(яркость) * усиление, не больше 255; все каналы масштабируются одинаково -
    // оттенок и насыщенность цвета сохраняются
    float newMax = gammaLut[(int)(mx + 0.5f)] * gain;
    if (newMax > 255.0f) newMax = 255.0f;
    float f = newMax / mx;
    buf[i] = display->color565((uint8_t)(r * f + 0.5f), (uint8_t)(g * f + 0.5f), (uint8_t)(b * f + 0.5f));
  }
  display->drawRGBBitmap(x, y, buf, w, h);
}

void drawRGBBitmapBoost(int x, int y, const uint16_t *bmp, int w, int h) {
  drawRGBBitmapBoostGain(x, y, bmp, w, h, ICON_GAIN);
}
