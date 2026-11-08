#pragma once
#include <Arduino.h>

// ============================================================
// Иконки погоды (10x10, RGB444) - по 2 кадра анимации на каждый код.
// СТАТУС: ВСЕ 22 - реальные данные (получены полностью).
// ============================================================
extern const uint16_t PROGMEM image_data_Image[100];    // ясно, день, кадр 1
extern const uint16_t PROGMEM image_data_Image1[100];   // ясно, день, кадр 2
extern const uint16_t PROGMEM image_data_Image2[100];   // туман, кадр 1
extern const uint16_t PROGMEM image_data_Image3[100];   // туман, кадр 2
extern const uint16_t PROGMEM image_data_Image4[100];   // снегопад, кадр 1
extern const uint16_t PROGMEM image_data_Image5[100];   // снегопад, кадр 2
extern const uint16_t PROGMEM image_data_Image6[100];   // снег (лёгкий), кадр 1
extern const uint16_t PROGMEM image_data_Image7[100];   // снег (лёгкий), кадр 2
extern const uint16_t PROGMEM image_data_Image8[100];   // ливень, кадр 1
extern const uint16_t PROGMEM image_data_Image9[100];   // ливень, кадр 2
extern const uint16_t PROGMEM image_data_Image10[100];  // дождь, кадр 1
extern const uint16_t PROGMEM image_data_Image11[100];  // дождь, кадр 2
extern const uint16_t PROGMEM image_data_Image12[100];  // пасмурно 100%, кадр 1
extern const uint16_t PROGMEM image_data_Image13[100];  // пасмурно 100%, кадр 2
extern const uint16_t PROGMEM image_data_Image14[100];  // гроза, кадр 1
extern const uint16_t PROGMEM image_data_Image15[100];  // гроза, кадр 2
extern const uint16_t PROGMEM image_data_Image16[100];  // облачно, день, кадр 1
extern const uint16_t PROGMEM image_data_Image17[100];  // облачно, день, кадр 2
extern const uint16_t PROGMEM image_data_Image18[100];  // облачно, ночь, кадр 1
extern const uint16_t PROGMEM image_data_Image19[100];  // облачно, ночь, кадр 2
extern const uint16_t PROGMEM image_data_Image20[100];  // ясно, ночь, кадр 1
extern const uint16_t PROGMEM image_data_Image21[100];  // ясно, ночь, кадр 2

// ============================================================
// Иконки для ДОМАШНЕГО экрана (weather_screen.cpp аналог - см. Dom_ekr.ino)
// СТАТУС: реальные данные есть для всех 8 (готово полностью)
// ============================================================
extern const uint16_t PROGMEM image_data_Image22[144];  // термометр, 12x12, кадр 1
extern const uint16_t PROGMEM image_data_Image23[144];  // термометр, 12x12, кадр 2
extern const uint16_t PROGMEM image_data_Image24[144];  // барометр,  12x12, кадр 1
extern const uint16_t PROGMEM image_data_Image25[144];  // барометр,  12x12, кадр 2
extern const uint16_t PROGMEM image_data_Image26[120];  // влажность, 10x12, кадр 1
extern const uint16_t PROGMEM image_data_Image27[120];  // влажность, 10x12, кадр 2
extern const uint16_t PROGMEM image_data_Image28[144];  // "ДОМ", 12x12, кадр 1 (окно тёмное)
extern const uint16_t PROGMEM image_data_Image29[144];  // "ДОМ", 12x12, кадр 2 (окно горит)

// ============================================================
// Иконки направления ветра (9x9, RGB444) - компас, 8 направлений + штиль.
// СТАТУС: реальные данные есть для ВСЕХ 9 (готово полностью).
// ВАЖНО: в исходных ассетах имена/комментарии для Востока и Запада
// перепутаны местами (баг унаследован из старого проекта):
//   над image_data_W стоит комментарий "Восточный"
//   над image_data_E стоит комментарий "Западный"
// При отрисовке это учтено в bitmapForWindIcon() (см. wind.cpp) -
// сами массивы здесь объявлены как есть, без переименования.
// ============================================================
extern const uint16_t image_data_N[81];
extern const uint16_t image_data_S[81];
extern const uint16_t image_data_E[81];   // по факту "Западный" (см. выше)
extern const uint16_t image_data_W[81];   // по факту "Восточный" (см. выше)
extern const uint16_t image_data_NE[81];
extern const uint16_t image_data_NW[81];
extern const uint16_t image_data_SE[81];
extern const uint16_t image_data_SW[81];
extern const uint16_t image_data_NO[81];  // штиль/безветренно

// ============================================================
// Глаза (ночь/утро), 64x32 - 8 кадров общей анимации, используется в
// обоих направлениях (см. night_screen.cpp - точные таймингов кадров
// из оригинала getNreg()/getYtro()). СТАТУС: все 8 получены.
// БЕЗ PROGMEM, как в присланном файле.
// ============================================================
extern const uint16_t image_data_eyes[2048];
extern const uint16_t image_data_eyes1[2048];
extern const uint16_t image_data_eyes2[2048];
extern const uint16_t image_data_eyes3[2048];
extern const uint16_t image_data_eyes4[2048];
extern const uint16_t image_data_eyes5[2048];
extern const uint16_t image_data_eyes6[2048];
extern const uint16_t image_data_eyes7[2048];

// ============================================================
// Фоны "ночь"/"утро", 64x32, по 1 кадру (не анимированы). СТАТУС:
// noch/noch1/noch2 и ytro/ytro1/ytro2 получены. Используется noch как
// основной фон ночного "спящего" экрана (см. night_screen.cpp).
// ============================================================
extern const uint16_t PROGMEM image_data_noch[2048];
extern const uint16_t PROGMEM image_data_noch1[2048];
extern const uint16_t PROGMEM image_data_noch2[2048];
extern const uint16_t PROGMEM image_data_ytro[2048];
extern const uint16_t PROGMEM image_data_ytro1[2048];
extern const uint16_t PROGMEM image_data_ytro2[2048];
extern const uint16_t image_data_Ytro[2048];  // доп. вариант, без PROGMEM (как в присланном файле)

// ============================================================
// Точки-маркеры дня недели для ночного экрана (Nreg.ino) - два ряда:
// BLUE (5x3) отмечает СЕГОДНЯШНИЙ день (y=25), GREEN/RED/GREY (5x2)
// отмечают статус будильника на каждый день (y=28) - зелёный для
// будних дней когда включён, красный для выходных когда включён,
// серый когда выключен.
// ============================================================
extern const uint16_t PROGMEM image_data_GREEN[12];
extern const uint16_t PROGMEM image_data_RED[12];
extern const uint16_t PROGMEM image_data_GREY[12];
extern const uint16_t PROGMEM image_data_BLUE[15];
