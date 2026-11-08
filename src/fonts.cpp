#include "fonts.h"

// Старый массив - НЕ трогаем, используется в других экранах проекта
const GFXfont* fontList[3] = {
  &FreeSansBold9pt7a,
  &kongtext4pt7a,
  &Heebo7pt8b
};

// ===== СЕЛЕКТОРЫ ШРИФТОВ (как в оригинале Шилова) =====

// Часы:минуты, ОСНОВНОЙ экран. Подписи в веб-интерфейсе (в этом же
// порядке): электрон, простой, квадрат, тонкий1, тонкий2, классич, контурный
const GFXfont* clockFontVariants[CLOCK_FONT_COUNT] = {
  &FreeSansBold9pt7a,
  &FreeSansBold9pt7b,
  &FreeSansBold9pt7c,
  &FreeSansBold9pt7d,
  &FreeSansBold9pt7e,
  &FreeSansBold9pt7f,
  &FreeSansBold9pt7g
};

// Секунды/число месяца/день недели, ОСНОВНОЙ экран. Подписи в
// веб-интерфейсе (в этом же порядке): тонкий1, обычный, футурист1,
// футурист2, квадрат, тонкий2
const GFXfont* secFontVariants[SEC_FONT_COUNT] = {
  &kongtext4pt7a,
  &kongtext4pt7b,
  &kongtext4pt7c,
  &kongtext4pt7d,
  &kongtext4pt7e,
  &kongtext4pt7f
};

// БОЛЬШИЕ часы. В оригинале без словесных подписей - просто номера 0-12.
// Порядок вариантов a,b,c,d,e,f,g,h,i,k,l,m,n (буква "j" пропущена -
// так было в оригинальном Fonts.h Шилова, не опечатка)
const GFXfont* bigFontVariants[BIG_FONT_COUNT] = {
  &FreeSansOblique18pt7a,
  &FreeSansOblique18pt7b,
  &FreeSansOblique18pt7c,
  &FreeSansOblique18pt7d,
  &FreeSansOblique18pt7e,
  &FreeSansOblique18pt7f,
  &FreeSansOblique18pt7g,
  &FreeSansOblique18pt7h,
  &FreeSansOblique18pt7i,
  &FreeSansOblique18pt7k,
  &FreeSansOblique18pt7l,
  &FreeSansOblique18pt7m,
  &FreeSansOblique18pt7n
};

