#include "screen_fx.h"
#include <math.h>
#include "globals.h"
#include "pins.h"

// ---------- НАСТРОЙКИ ЭФФЕКТОВ ----------
// Для каких экранов включены эффекты: бит N = экран N (1=прогноз, 2=погода сейчас, 3=дома, 4=комнаты).
// Включено на всех четырёх: прогноз, погода сейчас, дома, комнаты (0b11110). Часы (бит 0) в эффекте не участвуют.
// Выключить какой-то экран - убрать его бит, например только прогноз и комнаты: (1u << 1) | (1u << 4).
static const uint32_t FX_SCREEN_MASK = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4);
static const unsigned long FX_SLIDE_MS = 900;      // сколько выплывает снизу                  <-- ПОДВИНУТЬ
static const unsigned long FX_DISSOLVE_MS = 1500;  // сколько рассыпается на пиксели           <-- ПОДВИНУТЬ
static const int FX_MAX_PARTICLES = 700;           // предел частиц (если точек больше - берём не все)

// ---------- ПЕРЕХВАТ РИСОВАНИЯ (FxPanel) ----------
void FxPanel::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (capture) capture->drawPixel(x, y, color);
  else MatrixPanel_I2S_DMA::drawPixel(x, y, color);
}
void FxPanel::fillScreen(uint16_t color) {
  if (capture) capture->fillScreen(color);
  else MatrixPanel_I2S_DMA::fillScreen(color);
}
#ifndef NO_FAST_FUNCTIONS
void FxPanel::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
  if (capture) capture->drawFastVLine(x, y, h, color);
  else MatrixPanel_I2S_DMA::drawFastVLine(x, y, h, color);
}
void FxPanel::drawFastVLine(int16_t x, int16_t y, int16_t h, uint8_t r, uint8_t g, uint8_t b) {
  if (capture) capture->drawFastVLine(x, y, h, color565(r, g, b));
  else MatrixPanel_I2S_DMA::drawFastVLine(x, y, h, r, g, b);
}
void FxPanel::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
  if (capture) capture->drawFastHLine(x, y, w, color);
  else MatrixPanel_I2S_DMA::drawFastHLine(x, y, w, color);
}
void FxPanel::drawFastHLine(int16_t x, int16_t y, int16_t w, uint8_t r, uint8_t g, uint8_t b) {
  if (capture) capture->drawFastHLine(x, y, w, color565(r, g, b));
  else MatrixPanel_I2S_DMA::drawFastHLine(x, y, w, r, g, b);
}
void FxPanel::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (capture) capture->fillRect(x, y, w, h, color);
  else MatrixPanel_I2S_DMA::fillRect(x, y, w, h, color);
}
void FxPanel::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t r, uint8_t g, uint8_t b) {
  if (capture) capture->fillRect(x, y, w, h, color565(r, g, b));
  else MatrixPanel_I2S_DMA::fillRect(x, y, w, h, r, g, b);
}
#endif

// ---------- СОСТОЯНИЕ ЭФФЕКТА ----------
enum FxState { FX_IDLE, FX_SLIDE, FX_LIVE, FX_DISSOLVE, FX_DONE };
static FxState s_state = FX_IDLE;
static unsigned long s_t0 = 0;           // начало текущей фазы
static GFXcanvas16 *s_cv = nullptr;      // снимок экрана 64x32

struct FxParticle {
  int8_t x0, y0;      // исходная точка
  uint16_t col;       // исходный цвет (RGB565)
  float vx, vy;       // скорость, пикселей за 20 мс
  float life;         // доля общего времени, сколько частица живёт (0.6..1.0)
};
static FxParticle *s_parts = nullptr;
static int s_pcount = 0;
static bool s_built = false;

static void freeParticles() {
  if (s_parts) { free(s_parts); s_parts = nullptr; }
  s_pcount = 0;
  s_built = false;
}

bool fxEnabledFor(int screen) {
  return screen >= 0 && screen < 32 && (FX_SCREEN_MASK & (1u << screen));
}

unsigned long fxExtraMs(int screen) {
  return fxEnabledFor(screen) ? FX_SLIDE_MS : 0;
}

void fxAbort() {
  s_state = FX_IDLE;
  freeParticles();
}

void fxEnter(int screen) {
  freeParticles();
  if (!fxEnabledFor(screen)) { s_state = FX_IDLE; return; }
  if (!s_cv) s_cv = new GFXcanvas16(PANEL_W, PANEL_H);
  if (!s_cv || !s_cv->getBuffer()) { s_state = FX_IDLE; return; }   // не хватило памяти - без эффектов
  s_state = FX_SLIDE;
  s_t0 = millis();
}

bool fxHoldSwitch(int screen) {
  if (!fxEnabledFor(screen)) return false;
  if (s_state == FX_IDLE || s_state == FX_DONE) return false;       // эффекта нет или уже закончился
  if (s_state == FX_SLIDE || s_state == FX_LIVE) {
    s_state = FX_DISSOLVE;                                           // время вышло - начинаем рассыпание
    s_t0 = millis();
    s_built = false;
  }
  return true;
}

// Рисуем экран в картинку (на матрицу при этом ничего не попадает).
static void captureFrame(void (*drawFn)()) {
  FxPanel *p = static_cast<FxPanel *>(display);
  s_cv->fillScreen(0);
  p->capture = s_cv;
  drawFn();
  p->capture = nullptr;
}

// Выводим картинку на матрицу, сдвинув вниз на offsetY строк (чёрное не рисуем).
static void blitCapture(int offsetY) {
  for (int y = 0; y < PANEL_H; y++) {
    int yy = y + offsetY;
    if (yy < 0 || yy >= PANEL_H) continue;
    for (int x = 0; x < PANEL_W; x++) {
      uint16_t c = s_cv->getPixel(x, y);
      if (c) display->drawPixel(x, yy, c);
    }
  }
}

static void buildParticles() {
  freeParticles();
  s_parts = (FxParticle *)malloc(sizeof(FxParticle) * FX_MAX_PARTICLES);
  if (!s_parts) return;
  int lit = 0;
  for (int y = 0; y < PANEL_H; y++)
    for (int x = 0; x < PANEL_W; x++)
      if (s_cv->getPixel(x, y)) lit++;
  int step = (lit > FX_MAX_PARTICLES) ? (lit / FX_MAX_PARTICLES + 1) : 1;   // слишком много точек - берём каждую N-ю
  int idx = 0;
  for (int y = 0; y < PANEL_H && s_pcount < FX_MAX_PARTICLES; y++) {
    for (int x = 0; x < PANEL_W && s_pcount < FX_MAX_PARTICLES; x++) {
      uint16_t c = s_cv->getPixel(x, y);
      if (!c) continue;
      if ((idx++ % step) != 0) continue;
      FxParticle &p = s_parts[s_pcount++];
      p.x0 = x; p.y0 = y; p.col = c;
      float angle = random(0, 360) * (PI / 180.0f);
      float speed = 0.15f + (random(0, 45) / 100.0f);
      p.vx = cosf(angle) * speed;
      p.vy = sinf(angle) * speed;
      p.life = 0.6f + random(0, 40) / 100.0f;
    }
  }
  s_built = true;
}

void fxDrawScreen(int screen, void (*drawFn)()) {
  if (!fxEnabledFor(screen) || s_state == FX_IDLE || s_state == FX_LIVE) {
    drawFn();
    return;
  }

  unsigned long now = millis();

  if (s_state == FX_SLIDE) {
    float p = (float)(now - s_t0) / (float)FX_SLIDE_MS;
    if (p >= 1.0f) {
      s_state = FX_LIVE;
      drawFn();
      return;
    }
    float inv = 1.0f - p;
    float e = 1.0f - inv * inv * inv;               // плавное замедление к концу
    int offsetY = (int)((1.0f - e) * PANEL_H + 0.5f);
    captureFrame(drawFn);                           // живой кадр (иконки продолжают двигаться)
    blitCapture(offsetY);
    return;
  }

  if (s_state == FX_DISSOLVE) {
    if (!s_built) {
      captureFrame(drawFn);                         // последний кадр экрана -> частицы
      buildParticles();
      s_t0 = millis();
      now = s_t0;
      if (!s_parts || s_pcount == 0) { s_state = FX_DONE; return; }
    }
    float tt = (float)(now - s_t0) / (float)FX_DISSOLVE_MS;
    if (tt >= 1.0f) {
      freeParticles();
      s_state = FX_DONE;
      return;
    }
    float frames = (float)(now - s_t0) / 20.0f;     // сколько "кадров по 20 мс" прошло
    for (int i = 0; i < s_pcount; i++) {
      FxParticle &p = s_parts[i];
      float age = tt / p.life;
      if (age >= 1.0f) continue;
      int x = (int)(p.x0 + p.vx * frames + 0.5f);
      int y = (int)(p.y0 + p.vy * frames + 0.5f);
      if (x < 0 || x >= PANEL_W || y < 0 || y >= PANEL_H) continue;
      float k = 1.0f - age;                          // гаснет по мере разлёта
      uint8_t r, g, b;
      MatrixPanel_I2S_DMA::color565to888(p.col, r, g, b);
      display->drawPixel(x, y, display->color565((uint8_t)(r * k), (uint8_t)(g * k), (uint8_t)(b * k)));
    }
    return;
  }

  // FX_DONE: пока main.cpp не переключил экран - чёрный кадр
  display->fillScreen(0);
}
