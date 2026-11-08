
#pragma once
// ============================================================
// ШРИФТЫ
// Все 27 файлов (см. include/Fonts/) - КАСТОМНЫЕ шрифты, подогнанные
// под матрицу 64x32, взяты из оригинального проекта Шилова - метрики
// трогать не нужно, они уже верные.
//
// TomThumb - самый мелкий (6x6) - используется ТОЛЬКО на экране
// "погода сейчас" (weather_screen.cpp), как и в оригинальном getNow():
// другие шрифты там не помещаются по ширине. В селекторы ниже не
// входит - используется напрямую через &TomThumb.
//
// Три селектора ниже (clockFontVariants/secFontVariants/bigFontVariants)
// повторяют выбор шрифта из оригинала (fch/fss/font в Portal.ino) -
// пользователь выбирает вариант через веб-интерфейс, индекс хранится
// в config.json (clockFontIndex/secFontIndex/bigFontIndex).
// ============================================================
#include <Adafruit_GFX.h>
#include "Fonts/FreeSansBold9pt7a.h"
#include "Fonts/FreeSansBold9pt7b.h"
#include "Fonts/FreeSansBold9pt7c.h"
#include "Fonts/FreeSansBold9pt7d.h"
#include "Fonts/FreeSansBold9pt7e.h"
#include "Fonts/FreeSansBold9pt7f.h"
#include "Fonts/FreeSansBold9pt7g.h"
#include "Fonts/kongtext4pt7a.h"
#include "Fonts/kongtext4pt7b.h"
#include "Fonts/kongtext4pt7c.h"
#include "Fonts/kongtext4pt7d.h"
#include "Fonts/kongtext4pt7e.h"
#include "Fonts/kongtext4pt7f.h"
#include "Fonts/Heebo7pt8b.h"
#include "Fonts/TomThumb.h"
#include "Fonts/FreeSansOblique18pt7a.h"
#include "Fonts/FreeSansOblique18pt7b.h"
#include "Fonts/FreeSansOblique18pt7c.h"
#include "Fonts/FreeSansOblique18pt7d.h"
#include "Fonts/FreeSansOblique18pt7e.h"
#include "Fonts/FreeSansOblique18pt7f.h"
#include "Fonts/FreeSansOblique18pt7g.h"
#include "Fonts/FreeSansOblique18pt7h.h"
#include "Fonts/FreeSansOblique18pt7i.h"
#include "Fonts/FreeSansOblique18pt7k.h"
#include "Fonts/FreeSansOblique18pt7l.h"
#include "Fonts/FreeSansOblique18pt7m.h"
#include "Fonts/FreeSansOblique18pt7n.h"

// Старый массив - НЕ трогаем, используется в других экранах проекта
// (weather_screen.cpp/forecast.cpp/home_screen.cpp и т.п.) через индексы
// FONT_INDEX_CLOCK/FONT_INDEX_SMALL/FONT_INDEX_MONTH (см. pins.h).
extern const GFXfont* fontList[3];

// ============================================================
// СЕЛЕКТОРЫ ШРИФТОВ - как в оригинале Шилова (27 вариантов = 7+6+13+1,
// Heebo7pt8b - без выбора, один единственный вариант для месяца)
// ============================================================
#define CLOCK_FONT_COUNT 7   // часы:минуты, ОСНОВНОЙ экран (fch в оригинале)
#define SEC_FONT_COUNT   6   // секунды/число/день недели, ОСНОВНОЙ экран (fss в оригинале)
#define BIG_FONT_COUNT   13  // БОЛЬШИЕ часы (font в оригинале)

extern const GFXfont* clockFontVariants[CLOCK_FONT_COUNT];
extern const GFXfont* secFontVariants[SEC_FONT_COUNT];
extern const GFXfont* bigFontVariants[BIG_FONT_COUNT];
