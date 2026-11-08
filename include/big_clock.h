#pragma once

// Включает большие часы (bigClockActive = true) И запускает анимацию
// выезда - направление берётся из настройки bigClockAnimMode
// (0=слева, 1=справа, 2=сверху, 3=снизу, 4=хаотично - тогда каждый
// раз выбирается случайное из первых четырёх). Используется вместо
// прямого присваивания bigClockActive = true - и из расписания
// (checkBigClockSchedule), и из веб-обработчика "включить сейчас".
void activateBigClock();

// Проверяет расписание "больших часов" (bigClockOnHour/Minute,
// bigClockOffHour/Minute) и включает/выключает bigClockActive в
// нужную минуту. Вызывать РАЗ В СЕКУНДУ (не каждый кадр).
// Включение идёт через activateBigClock() (с анимацией выезда),
// выключение - просто bigClockActive = false (без анимации ухода).
void checkBigClockSchedule();

// Рисует полноэкранные "большие часы" - крупный шрифт, мигающее
// двоеточие (через общий colonState, как в обычных часах).
// Вызывается ВМЕСТО всех остальных экранов, пока bigClockActive==true.
void drawBigClockScreen();
