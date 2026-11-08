#pragma once

// Пересобирает fullScrollText из данных о погоде
void updateFullScrollText();

// Рисует бегущую строку (вызывается из drawScreen)
void drawScrollText();

// Рисует весь основной экран: часы, дату, бегущую строку
void drawScreen();
