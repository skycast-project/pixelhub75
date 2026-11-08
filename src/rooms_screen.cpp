#include "rooms_screen.h"
#include <math.h>
#include <stdio.h>
#include "globals.h"
#include "fonts.h"
#include "utils.h"
#include "icons.h"
#include "icon_boost.h"
#include "icons_rooms8.h"
#include "local_sensors.h"

// ---------- РАСКЛАДКА: один датчик на экран (всё подгоняется здесь) ----------
// Матрица 64x32. Название по центру сверху. Дальше "ступенька":
//   по центру выше - температура (значок термометра строго по центру, число справа от него)
//   слева ниже   - давление или влажность (значок + число)
//   если есть и давление, и влажность - капля с влажностью ставится в нижней строке правее (ROOMS_HUM_ICON_X).
static const int ROOMS_PER_PAGE = 1;      // датчиков на экран
static const int ROOMS_W = 64;            // ширина области (весь экран)
static const int ROOMS_NAME_BASE = 5;     // название: базовая линия (как у названия месяца на часах); хвосты букв д/у/р идут ниже <-- ПОДВИНУТЬ
static const int ROOMS_MARGIN = 3;        // отступ блоков от левого/правого края экрана      <-- ПОДВИНУТЬ
static const int ROOMS_TEMP_DX = 0;       // термометр стоит строго по центру экрана; это ручная подгонка: минус - влево, плюс - вправо <-- ПОДВИНУТЬ
static const int ROOMS_HUM_ICON_X = 33;   // капля в нижней строке (при трёх величинах): левый край значка, ФИКСИРОВАН (число справа, "100%" ещё влезает) <-- ПОДВИНУТЬ
static const bool ROOMS_SMALL_ICONS = false;  // false - оригинальные 12x12 (как на других экранах), true - малые 8x8
static const bool ROOMS_SHOW_BOTH = true; // авто-режим: если у датчика есть и давление, и влажность - показать обе (3 величины)

// Большие иконки 12x12
static const int ROOMS_UP_Y = 7;          // верх иконки температуры (верхняя строка)         <-- ПОДВИНУТЬ
static const int ROOMS_LOW_Y = 20;        // верх иконок нижней строки                        <-- ПОДВИНУТЬ
static const int ROOMS_ICON_COL = 12;     // ширина колонки значка (число начинается правее)
static const int ROOMS_TEXT_GAP = 1;      // зазор между значком и числом
static const int ROOMS_TEXT_DY = 9;       // базовая линия числа ниже верха значка (термометр, барометр)
static const int ROOMS_DROP_TEXT_DY = 8;  // то же для капли
// Малые иконки 8x8
static const int ROOMS_S_UP_Y = 8;
static const int ROOMS_S_LOW_Y = 20;
static const int ROOMS_S_ICON_COL = 8;
static const int ROOMS_S_TEXT_DY = 6;

enum RoomsKind { RK_TEMP = 0, RK_PRESS = 1, RK_HUM = 2 };

bool roomsScreenReady() {
  return localSensorsRoomsEnabled() && localSensorsRoomsCount() > 0;
}

int roomsPageCount() {
  int n = localSensorsRoomsCount();
  return (n + ROOMS_PER_PAGE - 1) / ROOMS_PER_PAGE;
}

// Название только строчными буквами (русские и латинские): "Кухня" -> "кухня", "ROOM 1" -> "room 1".
static String roomsLowerUtf8(const String &u) {
  String out;
  const unsigned char *p = (const unsigned char *)u.c_str();
  while (*p) {
    uint32_t cp;
    if (*p < 0x80) { cp = *p++; }
    else if ((*p & 0xE0) == 0xC0 && p[1]) { cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
    else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { p += 3; continue; }   // редкие символы пропускаем
    else { p++; continue; }
    if (cp >= 'A' && cp <= 'Z') cp += 32;
    else if (cp >= 0x410 && cp <= 0x42F) cp += 32;   // А-Я -> а-я
    else if (cp == 0x401) cp = 0x451;                // Ё -> ё
    if (cp < 0x80) out += (char)cp;
    else { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
  }
  return out;
}

// Название по центру экрана строчными буквами шрифтом бегущей строки (Heebo7pt8b).
// Базовая линия фиксированная (ROOMS_NAME_BASE), чтобы название не "прыгало" от датчика к датчику.
// Слишком длинное сокращается по буквам.
static void drawName(const String &nameUtf8) {
  String name = roomsLowerUtf8(nameUtf8);
  display->setFont(&Heebo7pt8b);
  display->setTextWrap(false);
  display->setTextColor(homeTitleColor);
  int16_t x1, y1;
  uint16_t w, h;
  String shown = utf8rus(name);
  for (;;) {
    display->getTextBounds(shown.c_str(), 0, ROOMS_NAME_BASE, &x1, &y1, &w, &h);
    if ((int)w <= ROOMS_W - 1 || name.length() <= 2) break;
    unsigned cut = name.length() - 1;      // убираем последний символ UTF-8
    while (cut > 0 && (((unsigned char)name[cut]) & 0xC0) == 0x80) cut--;
    name = String(name.c_str(), cut);
    shown = utf8rus(name);
  }
  int x = (ROOMS_W - (int)w) / 2 - x1;
  if (x < 0) x = 0;
  display->setCursor(x, ROOMS_NAME_BASE);
  display->print(shown);
  display->setTextWrap(true);
  display->setFont();
}

// Рисует блок: значок (термометр/барометр/капля) слева, число справа. x - левый край блока, y - верх значка.
static void drawItem(int x, int y, RoomsKind kind, const char *txt, uint16_t color, bool frameFast, char sign = 0) {
  const bool sm = ROOMS_SMALL_ICONS;
  if (kind == RK_TEMP) {
    drawRGBBitmapBoost(x, y,
        sm ? (frameFast ? image_data_Image23_8x8 : image_data_Image22_8x8)
           : (frameFast ? image_data_Image23 : image_data_Image22), sm ? 8 : 12, sm ? 8 : 12);
  } else if (kind == RK_PRESS) {
    drawRGBBitmapBoost(x, y,
        sm ? (frameFast ? image_data_Image25_8x8 : image_data_Image24_8x8)
           : (frameFast ? image_data_Image25 : image_data_Image24), sm ? 8 : 12, sm ? 8 : 12);
  } else {
    if (sm) drawRGBBitmapBoost(x, y, frameFast ? image_data_Image27_8x8 : image_data_Image26_8x8, 8, 8);
    else    drawRGBBitmapBoost(x, y, frameFast ? image_data_Image27 : image_data_Image26, 10, 12);
  }
  int iconCol = sm ? ROOMS_S_ICON_COL : ROOMS_ICON_COL;
  int dy = sm ? ROOMS_S_TEXT_DY : (kind == RK_HUM ? ROOMS_DROP_TEXT_DY : ROOMS_TEXT_DY);
  display->setFont(&TomThumb);
  display->setTextWrap(false);
  display->setTextColor(color);
  int cx = x + iconCol + ROOMS_TEXT_GAP;
  // Знак "+"/"-" рисуем отдельно и с зазором 1 px, чтобы он не лип к цифрам.
  if (sign) {
    display->setCursor(cx, y + dy);
    display->print(sign);
    cx += 6;                 // 5 px символ + 1 px зазор
  }
  display->setCursor(cx, y + dy);
  display->print(txt);
  display->setTextWrap(true);
  display->setFont();
}

static void drawSensor(const LsView &v, bool frameFast, bool blinkSlow) {
  drawName(v.name);

  // --- тексты
  char tBuf[12], hBuf[12], pBuf[12];
  char tSign = 0;
  if (v.fresh && v.hasT) {
    int t = (int)round(v.t);
    tSign = (t < 0) ? '-' : '+';
    snprintf(tBuf, sizeof(tBuf), "%dc", abs(t));   // число без знака, знак отдельно
  } else {
    snprintf(tBuf, sizeof(tBuf), "--");
  }
  if (v.fresh && v.hasH) snprintf(hBuf, sizeof(hBuf), "%d%%", (int)round(v.h));
  else snprintf(hBuf, sizeof(hBuf), "--");
  if (v.fresh && v.hasP) snprintf(pBuf, sizeof(pBuf), "%d", (int)round(v.pMm));
  else snprintf(pBuf, sizeof(pBuf), "--");

  // --- какие величины показывать в нижней строке
  bool showHum, showPress;
  if (v.second == 1)      { showHum = true;  showPress = false; }
  else if (v.second == 2) { showHum = false; showPress = true;  }
  else if (ROOMS_SHOW_BOTH && v.hasH && v.hasP) { showHum = true; showPress = true; }
  else if (v.hasH)        { showHum = true;  showPress = false; }
  else if (v.hasP)        { showHum = false; showPress = true;  }
  else                    { showHum = true;  showPress = false; }   // нет данных: покажем "--" под каплей

  const int upY  = ROOMS_SMALL_ICONS ? ROOMS_S_UP_Y  : ROOMS_UP_Y;
  const int lowY = ROOMS_SMALL_ICONS ? ROOMS_S_LOW_Y : ROOMS_LOW_Y;

  // --- верхняя строка: температура, у правого края
  // значок термометра ФИКСИРОВАН по центру экрана и не двигается от длины числа; число рисуется справа от него
  int iconCol = ROOMS_SMALL_ICONS ? ROOMS_S_ICON_COL : ROOMS_ICON_COL;
  drawItem((ROOMS_W - iconCol) / 2 + ROOMS_TEMP_DX, upY, RK_TEMP, tBuf, homeTempColor, frameFast, tSign);

  // --- нижняя строка: слева первый показатель, при трёх величинах второй - у правого края
  if (showPress && showHum) {
    drawItem(ROOMS_MARGIN, lowY, RK_PRESS, pBuf, homePressureColor, frameFast);
    drawItem(ROOMS_HUM_ICON_X, lowY, RK_HUM, hBuf, homeHumidityColor, frameFast);
  } else if (showPress) {
    drawItem(ROOMS_MARGIN, lowY, RK_PRESS, pBuf, homePressureColor, frameFast);
  } else {
    drawItem(ROOMS_MARGIN, lowY, RK_HUM, hBuf, homeHumidityColor, frameFast);
  }

  // --- разряжена батарейка: красная точка 2x2 в правом верхнем углу, мигает
  if (v.lowBatt && blinkSlow) {
    uint16_t red = display->color565(255, 0, 0);
    int bx = ROOMS_W - 3;
    display->drawPixel(bx, 0, red);
    display->drawPixel(bx + 1, 0, red);
    display->drawPixel(bx, 1, red);
    display->drawPixel(bx + 1, 1, red);
  }
}

void drawRoomsScreen(int page) {
  int total = localSensorsRoomsCount();
  if (page < 0 || page >= total) return;

  // иконки мигают раз в 300 мс, как на экране «Дома»
  static unsigned long lastToggle = 0;
  static bool frame2 = false;
  unsigned long now = millis();
  if (now - lastToggle >= 300) {
    lastToggle = now;
    frame2 = !frame2;
  }
  bool blinkSlow = ((now / 500) % 2) == 0;

  LsView v;
  if (!localSensorsRoomsGet(page, v)) return;
  drawSensor(v, frame2, blinkSlow);
}
