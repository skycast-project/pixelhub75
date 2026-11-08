#pragma once

// Рисует одну иконку погоды (по WMO weathercode Open-Meteo) в (cx,cy).
// Публичная - переиспользуется на экране "погода сейчас" (weather_screen.cpp)
// для сегодняшней/завтрашней иконки.
void drawWeatherIcon(int weathercode, int cx, int cy);

// Рисует второй экран - прогноз на несколько дней (день недели,
// иконка погоды, диапазон температур). Переключается с основным
// экраном часов в loop() по таймеру.
void drawForecastScreen();
