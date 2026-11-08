#pragma once
// ============================================================
// Эффекты переходов экранов: выплывание снизу и рассыпание на пиксели.
//
// КАК РАБОТАЕТ
//  1) FxPanel - дочерний класс панели HUB75. Обычно ведёт себя как родитель, а в режиме
//     "снимок" (capture != nullptr) направляет всё рисование в картинку 64x32 в памяти.
//     Поэтому сами экраны (forecast.cpp и др.) менять не нужно.
//  2) Экран рисуется через fxDrawScreen(): первые FX_SLIDE_MS он выплывает снизу,
//     потом рисуется как обычно, в конце (когда время показа вышло) рассыпается на пиксели.
//  3) main.cpp сообщает о смене экрана (fxEnter) и спрашивает, не идёт ли рассыпание
//     (fxHoldSwitch), чтобы не переключить экран раньше времени.
//
// Включённые экраны задаются маской FX_SCREEN_MASK в screen_fx.cpp
// (номера как в main.cpp: 1=прогноз, 2=погода сейчас, 3=дома, 4=комнаты).
// ============================================================
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>

class FxPanel : public MatrixPanel_I2S_DMA {
 public:
  using MatrixPanel_I2S_DMA::MatrixPanel_I2S_DMA;   // те же конструкторы, что у родителя

  GFXcanvas16 *capture = nullptr;   // не nullptr - рисуем в картинку, а не на матрицу

  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void fillScreen(uint16_t color) override;
#ifndef NO_FAST_FUNCTIONS
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint8_t r, uint8_t g, uint8_t b) override;
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint8_t r, uint8_t g, uint8_t b) override;
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t r, uint8_t g, uint8_t b) override;
#endif
};

// Включён ли эффект для этого экрана.
bool fxEnabledFor(int screen);

// Сколько миллисекунд добавить ко времени показа экрана (на выплывание).
unsigned long fxExtraMs(int screen);

// Вызвать, когда на экран ПЕРЕКЛЮЧИЛИСЬ (начать выплывание).
void fxEnter(int screen);

// Вызвать, когда время показа вышло. true - идёт рассыпание, экран пока НЕ переключать.
bool fxHoldSwitch(int screen);

// Сбросить эффект (например, включился ночной режим или большие часы).
void fxAbort();

// Нарисовать экран с эффектами. drawFn - обычная функция рисования экрана.
void fxDrawScreen(int screen, void (*drawFn)());
