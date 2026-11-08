#include "forecast.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "globals.h"
#include "fonts.h"
#include "icons.h"
#include "icon_boost.h"
#include "font_days4x5.h"

// ============================================================
// Конвертация WMO-кода (Open-Meteo) в код в духе OpenWeatherMap -
// та же логика, что в weather.cpp для location_code/coded1, чтобы
// иконка совпадала с тем, что описывает текст погоды.
// ============================================================
static int owmCodeFromWmo(int wmo) {
  if (wmo == 0)  return 800;
  if (wmo == 1)  return 801;
  if (wmo == 2)  return 802;
  if (wmo == 3)  return 804;
  if (wmo == 45) return 701;
  if (wmo == 48) return 600;
  if (wmo >= 51 && wmo <= 67) return 300;
  if (wmo >= 71 && wmo <= 77) return 601;
  if (wmo == 80 || wmo == 81 || wmo == 82) return 313;
  if (wmo == 85 || wmo == 86) return 601;
  if (wmo == 95 || wmo == 96 || wmo == 99) return 200;
  return 800;
}

struct IconFramePair { const uint16_t *frame1; const uint16_t *frame2; };

static IconFramePair iconFramesForOwm(int owm, bool forceDay) {
  bool isDay = forceDay || (hours >= 6 && hours < 21);
  switch (owm) {
    case 800: return isDay ? IconFramePair{image_data_Image, image_data_Image1}
                            : IconFramePair{image_data_Image20, image_data_Image21};
    case 801:
    case 802:
    case 803:
      return isDay ? IconFramePair{image_data_Image16, image_data_Image17}
                   : IconFramePair{image_data_Image18, image_data_Image19};
    case 804: return {image_data_Image12, image_data_Image13};
    case 701: return {image_data_Image2, image_data_Image3};
    case 600: return {image_data_Image6, image_data_Image7};
    case 601: return {image_data_Image4, image_data_Image5};
    case 300: return {image_data_Image10, image_data_Image11};
    case 313: return {image_data_Image8, image_data_Image9};
    case 200: return {image_data_Image14, image_data_Image15};
    default:  return isDay ? IconFramePair{image_data_Image, image_data_Image1}
                            : IconFramePair{image_data_Image20, image_data_Image21};
  }
}

// Выбор иконки по коду погоды Open-Meteo (WMO weather code).
// Публичная - переиспользуется на экране "погода сейчас" (weather_screen.cpp).
// Все 22 растровых кадра подключены (icons.cpp). У каждой погоды 2 кадра
// анимации (как в старом проекте) - переключаются раз в 500мс через
// millis(), БЕЗ delay() (в отличие от старого getImeg()).
// (cx,cy) - ЦЕНТР иконки (10x10), не левый верхний угол.
// true  - на экране "погода сейчас" рисуются яркие иконки экрана на 3 дня (без анимации);
// false - прежние растровые иконки с анимацией (см. усиление яркости ниже).
static const bool WEATHER_NOW_BRIGHT_ICONS = false;
// Яркость старых иконок усиливается функцией drawRGBBitmapBoost (icon_boost.cpp: ICON_GAIN, ICON_GAMMA).

// Объявления заранее: сами иконки и drawIcon10 описаны ниже по файлу.
static void drawIcon10(int x, int y, const char *const *rows);
static const char *const *forecastIconFor(int weathercode);

// Яркость иконок теперь настраивается в одном месте - icon_boost.cpp (ICON_GAIN, ICON_GAMMA)
static void drawBoostedBitmap(int x, int y, const uint16_t *bmp) {
  drawRGBBitmapBoost(x, y, bmp, 10, 10);
}

// forceDay=true - всегда дневные иконки (для прогноза на дни вперёд), false - день/ночь по текущему часу
static void drawWeatherIconImpl(int weathercode, int cx, int cy, bool forceDay) {
  if (WEATHER_NOW_BRIGHT_ICONS) {
    drawIcon10(cx - 5, cy - 5, forecastIconFor(weathercode));
    return;
  }
  int owm = owmCodeFromWmo(weathercode);
  IconFramePair frames = iconFramesForOwm(owm, forceDay);

  static unsigned long lastToggle = 0;
  static bool showFrame2 = false;
  unsigned long now = millis();
  if (now - lastToggle >= 500) {
    lastToggle = now;
    showFrame2 = !showFrame2;
  }

  const uint16_t *bmp = showFrame2 ? frames.frame2 : frames.frame1;
  drawBoostedBitmap(cx - 5, cy - 5, bmp);
}

void drawWeatherIcon(int weathercode, int cx, int cy) {
  drawWeatherIconImpl(weathercode, cx, cy, false);
}

// ============================================================
// ЭКРАН ПРОГНОЗА: 3 колонки - день недели / иконка 10x10 / макс и мин °C
//
// Раскладка по вертикали (экран 32 строки, номера строк с нуля):
//   день недели (свой шрифт 4x5) ... строки 2..6   (базовая линия y=7)
//   иконка 10x10 ................... строки 9..18
//   верхняя температура ............ строки 21..25 (базовая линия 26)
//   нижняя температура ............. строки 27..31 (базовая линия 32)
// По умолчанию сверху максимум (стрелка вверх), снизу минимум (стрелка вниз).
//
// Иконки здесь СВОИ (отличаются от экрана "Погода сейчас"), нарисованы
// символами: каждая строка - 10 символов, буква = цвет (см. iconColor()),
// '.' = прозрачный пиксель. Чтобы заменить иконку - перерисуйте строки.
// ============================================================

// ---------- РАСКЛАДКА (всё подгоняется здесь) ----------
static const int FORECAST_COL_X[FORECAST_DAYS] = {1, 23, 44};  // левый край колонок <-- ПОДВИНУТЬ
// Какие дни показывает экран: номер первого дня от сегодняшнего (0=сегодня, 1=завтра, 2=послезавтра...).
// 2 - экран показывает дни +2, +3, +4 (названия: СР, ЧТ, ПТ...), а «сегодня/завтра» остаются на экране «Погода сейчас».
// 0 - вернуть прежнее: сегодня, завтра, послезавтра.   (FORECAST_FIRST_DAY + FORECAST_DAYS не больше FORECAST_DATA_DAYS)
static const int FORECAST_FIRST_DAY = 2;
static const int FORECAST_COL_W = 21;      // ширина колонки (для центрирования)
static const int FORECAST_DAY_Y = 5;       // день недели: базовая линия (глиф занимает 5 строк выше неё) <-- ПОДВИНУТЬ
static const int FORECAST_DAY_DX = 4;      // день недели: отступ слева, ТОЛЬКО для старых шрифтов (FORECAST_OWN_DAY_FONT=false)
static const int FORECAST_ICON_SIZE = 10;  // размер иконки (в пикселях, сами картинки нарисованы 10x10)
static const int FORECAST_ICON_DX = 5;     // иконка: отступ от левого края колонки (по центру 21 px) <-- ПОДВИНУТЬ
static const int FORECAST_ICON_Y = 6;      // иконка: верхний край                     <-- ПОДВИНУТЬ
static const int FORECAST_LINE1_Y = 24;    // верхняя температура: базовая линия       <-- ПОДВИНУТЬ
static const int FORECAST_LINE2_Y = 31;    // нижняя температура: базовая линия        <-- ПОДВИНУТЬ
static const bool FORECAST_MAX_ON_TOP = true;   // true: сверху максимум, снизу минимум; false - наоборот
static const bool FORECAST_SHOW_ARROWS = true;  // маленькие стрелки вверх/вниз перед числами

// ---------- НАЗВАНИЯ ДНЕЙ ----------
// true  - свой шрифт 4x5 (font_days4x5.h), всегда по центру колонки;
// false - как раньше: шрифт из списка в вебе (forecastDayFontIndex) и dayNames[].
static const bool FORECAST_OWN_DAY_FONT = true;
// Как нумеруется wdayNum в проекте: true - 0 = воскресенье (как tm_wday), false - 0 = понедельник.
// Если на матрице дни сдвинуты на один (под сегодняшним числом стоит вчерашний или завтрашний
// день недели) - переключите это значение.
static const bool FORECAST_WDAY_SUNDAY_FIRST = true;
// Названия дней в кодах шрифта font_days4x5.h, порядок: ВС ПН ВТ СР ЧТ ПТ СБ
static const char *const OWN_DAY_NAMES[7] = {"CE", "AB", "CD", "EF", "GD", "AD", "EH"};

// ---------- ИКОНКИ 10x10 ----------
static const char *const ICON_SUN[10] = {
  "....OO....",
  ".O..OO..O.",
  "...YYYY...",
  "..YYYYYY..",
  "OOYYYYYYOO",
  "OOYYYYYYOO",
  "..YYYYYY..",
  "...YYYY...",
  ".O..OO..O.",
  "....OO....",
};

static const char *const ICON_PARTLY[10] = {
  "..O.......",
  ".OYYO.....",
  ".YYYY.WW..",
  "OYYYYWWWW.",
  ".YYWWWWWWW",
  "..WWWWWWWW",
  ".WWWWWWWWW",
  ".WWWWWWWWW",
  "..GGGGGGG.",
  "..........",
};

static const char *const ICON_CLOUDY[10] = {
  "..........",
  "..........",
  "...GGGG...",
  "..GGGGGG..",
  ".GGGGGGGG.",
  "GGGGGGGGGG",
  "GGGGGGGGGG",
  ".DDDDDDDD.",
  "..........",
  "..........",
};

static const char *const ICON_FOG[10] = {
  "...FFFF...",
  "..FFFFFF..",
  ".FFFFFFFF.",
  "..........",
  ".FFFFFFFF.",
  "..........",
  "..FFFFFF..",
  "..........",
  ".FFFFFFFF.",
  "..........",
};

static const char *const ICON_RAIN[10] = {
  "..........",
  "...GGGG...",
  "..GGGGGG..",
  ".GGGGGGGG.",
  "GGGGGGGGGG",
  ".DDDDDDDD.",
  "..........",
  "..B..B..B.",
  ".B..B..B..",
  "..........",
};

static const char *const ICON_SHOWER[10] = {
  "..........",
  "...DDDD...",
  "..DDDDDD..",
  ".DDDDDDDD.",
  "DDDDDDDDDD",
  ".GGGGGGGG.",
  "..........",
  ".B.B.B.B..",
  "B.B.B.B.B.",
  ".B.B.B.B..",
};

static const char *const ICON_THUNDER[10] = {
  "..........",
  "...DDDD...",
  "..DDDDDD..",
  ".DDDDDDDD.",
  "DDDDDDDDDD",
  ".GGGGGGGG.",
  ".....LL...",
  "....LL....",
  "...LLLL...",
  ".....L....",
};

static const char *const ICON_SNOW[10] = {
  "..........",
  "...GGGG...",
  "..GGGGGG..",
  ".GGGGGGGG.",
  "GGGGGGGGGG",
  ".GGGGGGGG.",
  "..........",
  "..S...S...",
  ".SSS.SSS..",
  "..S...S...",
};

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Буква -> цвет. Возвращает false для прозрачного пикселя.
static bool iconColor(char c, uint16_t &out) {
  switch (c) {
    case 'Y': out = rgb565(255, 220, 0);   return true;  // солнце
    case 'O': out = rgb565(255, 140, 0);   return true;  // лучи
    case 'W': out = rgb565(235, 235, 245); return true;  // облако светлое
    case 'G': out = rgb565(150, 160, 175); return true;  // облако серое
    case 'D': out = rgb565(110, 120, 140); return true;  // облако тёмное
    case 'F': out = rgb565(170, 180, 190); return true;  // туман
    case 'B': out = rgb565(0, 120, 255);   return true;  // капли дождя
    case 'L': out = rgb565(255, 230, 0);   return true;  // молния
    case 'S': out = rgb565(255, 255, 255); return true;  // снег
    default:  return false;                              // '.' - прозрачно
  }
}

static void drawIcon10(int x, int y, const char *const *rows) {
  for (int r = 0; r < FORECAST_ICON_SIZE; r++) {
    for (int c = 0; c < FORECAST_ICON_SIZE; c++) {
      uint16_t col;
      if (iconColor(rows[r][c], col)) display->drawPixel(x + c, y + r, col);
    }
  }
}

// Иконка по коду погоды (WMO, как в forecastWeathercode[]).
static const char *const *forecastIconFor(int weathercode) {
  switch (owmCodeFromWmo(weathercode)) {
    case 800: return ICON_SUN;
    case 801:
    case 802:
    case 803: return ICON_PARTLY;
    case 804: return ICON_CLOUDY;
    case 701: return ICON_FOG;
    case 600:
    case 601: return ICON_SNOW;
    case 300: return ICON_RAIN;
    case 313: return ICON_SHOWER;
    case 200: return ICON_THUNDER;
    default:  return ICON_SUN;
  }
}

// ---------- МАЛЕНЬКИЕ АНИМИРОВАННЫЕ ИКОНКИ 8x8 ----------
// Яркие (цвета те же, что у иконок 10x10), по 2 кадра, кадры переключаются раз в 500 мс.
// Буква = цвет (см. iconColor()), '.' = прозрачный пиксель.
// FORECAST_SMALL_ICONS = false - вернуться к прежним неподвижным иконкам 10x10.
// Вид иконок на экране прогноза:
//   0 - как на экране "Погода сейчас" (растровые 10x10 с анимацией, общая яркость icon_boost) - по умолчанию
//   1 - маленькие яркие 8x8 с анимацией (нарисованы символами ниже)
//   2 - прежние неподвижные 10x10 (ICON_SUN и т.д.)
static const int FORECAST_ICON_STYLE = 0;
// Что показывать из температуры: 0 - только максимум, 1 - только минимум, 2 - обе строки (максимум и минимум)
static const int FORECAST_TEMP_MODE = 0;
// Раскладка, когда температура ОДНА (FORECAST_TEMP_MODE 0 или 1):
static const int FORECAST_ONE_ICON_Y = 10;   // иконка (10 px высотой): верхний край       <-- ПОДВИНУТЬ
static const int FORECAST_ONE_TEMP_Y = 29;   // температура: базовая линия (цифры 5 строк выше) <-- ПОДВИНУТЬ
static const bool FORECAST_ONE_ARROW = false; // стрелка вверх/вниз перед одиночной температурой
static const int FORECAST_ICON8_DX = 6;    // 8x8: отступ от левого края колонки (по центру 21 px) <-- ПОДВИНУТЬ
static const int FORECAST_ICON8_Y = 9;     // 8x8: верхний край иконки                            <-- ПОДВИНУТЬ

static const char *const ICON8_SUN_A[8] = {
  "...OO...",
  "........",
  "..YYYY..",
  "O.YYYY.O",
  "O.YYYY.O",
  "..YYYY..",
  "........",
  "...OO...",
};
static const char *const ICON8_SUN_B[8] = {
  "........",
  ".O....O.",
  "..YYYY..",
  "..YYYY..",
  "..YYYY..",
  "..YYYY..",
  ".O....O.",
  "........",
};
static const char *const ICON8_PARTLY_A[8] = {
  "..O.....",
  ".YYY....",
  "OYYYWW..",
  ".YYWWWW.",
  "..WWWWWW",
  ".WWWWWWW",
  ".GGGGGGG",
  "........",
};
static const char *const ICON8_PARTLY_B[8] = {
  "O...O...",
  ".YYY....",
  ".YYYWW..",
  ".YYWWWW.",
  "O.WWWWWW",
  ".WWWWWWW",
  ".GGGGGGG",
  "........",
};
static const char *const ICON8_CLOUDY_A[8] = {
  "........",
  "..GGGG..",
  ".GGGGGG.",
  "GGGGGGGG",
  "GGGGGGGG",
  ".DDDDDD.",
  "........",
  "........",
};
static const char *const ICON8_CLOUDY_B[8] = {
  "........",
  "........",
  "..GGGG..",
  ".GGGGGG.",
  "GGGGGGGG",
  "GGGGGGGG",
  ".DDDDDD.",
  "........",
};
static const char *const ICON8_FOG_A[8] = {
  "........",
  ".FFFFFF.",
  "........",
  "..FFFFFF",
  "........",
  ".FFFFFF.",
  "........",
  "FFFFFF..",
};
static const char *const ICON8_FOG_B[8] = {
  "........",
  "FFFFFF..",
  "........",
  ".FFFFFF.",
  "........",
  "..FFFFFF",
  "........",
  ".FFFFFF.",
};
static const char *const ICON8_RAIN_A[8] = {
  "..GGGG..",
  ".GGGGGG.",
  "GGGGGGGG",
  ".DDDDDD.",
  "........",
  ".B..B..B",
  "........",
  "..B..B..",
};
static const char *const ICON8_RAIN_B[8] = {
  "..GGGG..",
  ".GGGGGG.",
  "GGGGGGGG",
  ".DDDDDD.",
  "..B..B..",
  "........",
  ".B..B..B",
  "........",
};
static const char *const ICON8_SHOWER_A[8] = {
  "..DDDD..",
  ".DDDDDD.",
  "DDDDDDDD",
  ".GGGGGG.",
  "B..B..B.",
  ".B..B..B",
  "B..B..B.",
  ".B..B..B",
};
static const char *const ICON8_SHOWER_B[8] = {
  "..DDDD..",
  ".DDDDDD.",
  "DDDDDDDD",
  ".GGGGGG.",
  ".B..B..B",
  "B..B..B.",
  ".B..B..B",
  "B..B..B.",
};
static const char *const ICON8_THUNDER_A[8] = {
  "..DDDD..",
  ".DDDDDD.",
  "DDDDDDDD",
  ".GGGGGG.",
  "....OO..",
  "...OO...",
  "..OOOO..",
  "....O...",
};
static const char *const ICON8_THUNDER_B[8] = {
  "..GGGG..",
  ".GGGGGG.",
  "GGGGGGGG",
  ".GGGGGG.",
  "....LL..",
  "...LL...",
  "..LLLL..",
  "....L...",
};
static const char *const ICON8_SNOW_A[8] = {
  "..GGGG..",
  ".GGGGGG.",
  "GGGGGGGG",
  ".DDDDDD.",
  "........",
  ".S....S.",
  "...S....",
  "S....S..",
};
static const char *const ICON8_SNOW_B[8] = {
  "..GGGG..",
  ".GGGGGG.",
  "GGGGGGGG",
  ".DDDDDD.",
  "S....S..",
  "........",
  ".S....S.",
  "...S....",
};

struct Icon8Pair { const char *const *a; const char *const *b; };

static Icon8Pair forecastIcon8For(int weathercode) {
  switch (owmCodeFromWmo(weathercode)) {
    case 800: return {ICON8_SUN_A, ICON8_SUN_B};
    case 801:
    case 802:
    case 803: return {ICON8_PARTLY_A, ICON8_PARTLY_B};
    case 804: return {ICON8_CLOUDY_A, ICON8_CLOUDY_B};
    case 701: return {ICON8_FOG_A, ICON8_FOG_B};
    case 600:
    case 601: return {ICON8_SNOW_A, ICON8_SNOW_B};
    case 300: return {ICON8_RAIN_A, ICON8_RAIN_B};
    case 313: return {ICON8_SHOWER_A, ICON8_SHOWER_B};
    case 200: return {ICON8_THUNDER_A, ICON8_THUNDER_B};
    default:  return {ICON8_SUN_A, ICON8_SUN_B};
  }
}

static void drawIcon8(int x, int y, const char *const *rows) {
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      uint16_t col;
      if (iconColor(rows[r][c], col)) display->drawPixel(x + c, y + r, col);
    }
  }
}

// ---------- ТЕМПЕРАТУРА ----------
// Температура со знаком, как на экране "Погода сейчас": +5, -3; ноль без знака.
static void formatTemp(char *buf, size_t n, int value) {
  if (value == 0) snprintf(buf, n, "0");
  else snprintf(buf, n, "%+d", value);
}

// Стрелка 3x2 пикселя: вверх (максимум) или вниз (минимум).
static void drawArrow(int x, int y, bool up, uint16_t color) {
  if (up) {
    display->drawPixel(x + 1, y, color);
    display->drawPixel(x, y + 1, color);
    display->drawPixel(x + 1, y + 1, color);
    display->drawPixel(x + 2, y + 1, color);
  } else {
    display->drawPixel(x, y, color);
    display->drawPixel(x + 1, y, color);
    display->drawPixel(x + 2, y, color);
    display->drawPixel(x + 1, y + 1, color);
  }
}

// Знак температуры всегда своим цветом, независимо от цвета цифр: плюс красный, минус синий.
// Цвета подбираются здесь: <-- ПОДВИНУТЬ
static const bool SIGN_COLORS_ON = true;     // false - знак того же цвета, что и цифры
static inline uint16_t signPlusColor()  { return display->color565(255, 0, 0); }
static inline uint16_t signMinusColor() { return display->color565(0, 120, 255); }

// Печатает строку с температурой от текущей позиции курсора: знак (+ или -) своим цветом, остальное цветом цифр.
static void printSignedTemp(const char *buf, uint16_t numColor) {
  char first = buf[0];
  if (SIGN_COLORS_ON && (first == '+' || first == '-')) {
    display->setTextColor(first == '+' ? signPlusColor() : signMinusColor());
    display->print(first);
    buf++;
  }
  display->setTextColor(numColor);
  display->print(buf);
}

// Одна строка температуры: [стрелка] число, по центру колонки.
// baseline - базовая линия шрифта TomThumb (цифры занимают 5 строк выше неё).
static void drawTempLine(int colX, int baseline, int value, uint16_t color, bool up) {
  char buf[8];
  formatTemp(buf, sizeof(buf), value);
  int textW = (int)strlen(buf) * 4 - 1;   // TomThumb: шаг 4 пикселя, ширина глифа 3
  int groupW = FORECAST_SHOW_ARROWS ? (4 + textW) : textW;
  int x0 = colX + (FORECAST_COL_W - groupW) / 2;

  display->setFont(&TomThumb);
  display->setTextColor(color);
  if (FORECAST_SHOW_ARROWS) {
    drawArrow(x0, baseline - 4, up, color);
    display->setCursor(x0 + 4, baseline);
  } else {
    display->setCursor(x0, baseline);
  }
  printSignedTemp(buf, color);
}

// ---------- ДЕНЬ НЕДЕЛИ ----------
static void drawDayName(int colX, int weekdayIdx, int dayFont) {
  display->setTextSize(1);
  display->setTextColor(forecastDayColor);

  if (FORECAST_OWN_DAY_FONT) {
    // weekdayIdx считается от wdayNum; приводим к порядку "воскресенье первым"
    int idx = FORECAST_WDAY_SUNDAY_FIRST ? weekdayIdx : (weekdayIdx + 1) % 7;
    const char *name = OWN_DAY_NAMES[idx];
    display->setFont(&DayFont4x5);
    int16_t x1, y1;
    uint16_t w, h;
    display->getTextBounds(name, 0, FORECAST_DAY_Y, &x1, &y1, &w, &h);
    int x = colX + (FORECAST_COL_W - (int)w) / 2 - x1;   // по центру колонки
    display->setCursor(x, FORECAST_DAY_Y);
    display->print(name);
  } else {
    // Старый вариант: шрифт из списка шрифтов секунд/даты (выбирается в вебе),
    // короткие имена dayNames уже в кодировке этих шрифтов.
    display->setFont(secFontVariants[dayFont]);
    display->setCursor(colX + FORECAST_DAY_DX, FORECAST_DAY_Y);
    display->print(dayNames[weekdayIdx]);
  }
}

// ---------- ИКОНКА И ОДИНОЧНАЯ ТЕМПЕРАТУРА ----------
// Рисует иконку в колонке; topY - верхний край 10-пиксельной области иконки.
static void drawForecastIcon(int code, int colX, int topY) {
  if (FORECAST_ICON_STYLE == 0) {
    // как на экране "Погода сейчас": анимированные растровые, всегда дневные (это прогноз на дни вперёд)
    drawWeatherIconImpl(code, colX + FORECAST_COL_W / 2, topY + 5, true);
  } else if (FORECAST_ICON_STYLE == 1) {
    Icon8Pair ic = forecastIcon8For(code);
    bool frame2 = ((millis() / 500) % 2) == 1;
    drawIcon8(colX + (FORECAST_COL_W - 8) / 2, topY + 1, frame2 ? ic.b : ic.a);
  } else {
    drawIcon10(colX + FORECAST_ICON_DX, topY, forecastIconFor(code));
  }
}

// Одна температура по центру колонки: [стрелка] число. Знак минус - от самого числа.
// true: формат как на экране «Погода сейчас» - «+ 5°C», «-3°C», «0°C» (буква c в шрифте = знак °C)
static const bool FORECAST_TEMP_LIKE_WEATHER = true;

static void drawOneTemp(int colX, int baseline, int value, uint16_t color, bool up) {
  if (FORECAST_ONE_ARROW) {
    drawTempLine(colX, baseline, value, color, up);
    return;
  }
  char buf[10];
  int16_t x1, y1;
  uint16_t w, h;
  display->setFont(&TomThumb);
  if (FORECAST_TEMP_LIKE_WEATHER) {
    if (value > 0) snprintf(buf, sizeof(buf), "+ %dc", value);
    else snprintf(buf, sizeof(buf), "%dc", value);
    display->getTextBounds(buf, 0, baseline, &x1, &y1, &w, &h);
    if ((int)w > FORECAST_COL_W) {            // двузначное с пробелом не влезает - убираем пробел после плюса
      if (value > 0) snprintf(buf, sizeof(buf), "+%dc", value);
      display->getTextBounds(buf, 0, baseline, &x1, &y1, &w, &h);
    }
  } else {
    formatTemp(buf, sizeof(buf), value);
    display->getTextBounds(buf, 0, baseline, &x1, &y1, &w, &h);
  }
  display->setTextColor(color);
  display->setCursor(colX + (FORECAST_COL_W - (int)w) / 2 - x1, baseline);
  printSignedTemp(buf, color);
}

// ============================================================
void drawForecastScreen() {
  int dayFont = forecastDayFontIndex;
  if (dayFont < 0 || dayFont >= SEC_FONT_COUNT) dayFont = 0;

  for (int i = 0; i < FORECAST_DAYS; i++) {
    int colX = FORECAST_COL_X[i];
    int dayIdx = FORECAST_FIRST_DAY + i;                  // индекс в массивах прогноза
    if (dayIdx >= FORECAST_DATA_DAYS) dayIdx = FORECAST_DATA_DAYS - 1;
    int weekdayIdx = (wdayNum + dayIdx) % 7;

    drawDayName(colX, weekdayIdx, dayFont);

    int tMax = (int)round(forecastTempMax[dayIdx]);
    int tMin = (int)round(forecastTempMin[dayIdx]);
    int code = forecastWeathercode[dayIdx];

    if (FORECAST_TEMP_MODE == 2) {
      // две строки температуры - раскладка как раньше (иконка выше, цифры в две строки)
      drawForecastIcon(code, colX, FORECAST_ICON_Y);
      if (FORECAST_MAX_ON_TOP) {
        drawTempLine(colX, FORECAST_LINE1_Y, tMax, forecastTempMaxColor, true);
        drawTempLine(colX, FORECAST_LINE2_Y, tMin, forecastTempMinColor, false);
      } else {
        drawTempLine(colX, FORECAST_LINE1_Y, tMin, forecastTempMinColor, false);
        drawTempLine(colX, FORECAST_LINE2_Y, tMax, forecastTempMaxColor, true);
      }
    } else {
      // одна температура - больше места: иконка по центру, цифры под ней
      drawForecastIcon(code, colX, FORECAST_ONE_ICON_Y);
      bool showMax = (FORECAST_TEMP_MODE == 0);
      drawOneTemp(colX, FORECAST_ONE_TEMP_Y, showMax ? tMax : tMin,
                  showMax ? forecastTempMaxColor : forecastTempMinColor, showMax);
    }
  }

  display->setFont();
}
