#pragma once
#include <Arduino.h>

// ============================================================
// Усиление яркости растровых иконок (погода, «Дома», «Комнаты», компас ветра).
// Рисует так же, как display->drawRGBBitmap(x, y, bmp, w, h), но ярче:
//   - яркость каждого пикселя подтягивается «гаммой» (ICON_GAMMA) и усилением (ICON_GAIN);
//   - цвет не «выгорает» в белый: оттенок и насыщенность сохраняются, самый яркий канал
//     пикселя упирается максимум в 255.
// Чёрный (фон) остаётся чёрным.
// Настройки - в начале icon_boost.cpp (ICON_GAMMA, ICON_GAIN).
// Использование: просто заменить  display->drawRGBBitmap(  на  drawRGBBitmapBoost(
// ============================================================
void drawRGBBitmapBoost(int x, int y, const uint16_t *bmp, int w, int h);

// Свой коэффициент для конкретной иконки (1.0 = без усиления; ICON_GAMMA действует всегда)
void drawRGBBitmapBoostGain(int x, int y, const uint16_t *bmp, int w, int h, float gain);
