#pragma once
#include <Arduino.h>

// DFPlayer Mini по UART2 (TX=26, RX=27). Не конфликтует с I2S матрицы.
void dfpInit();
bool dfpReady();
void dfpSetVolume(uint8_t vol);     // 0..30
uint8_t dfpVolume();
bool dfpPlayTrack(uint16_t n);
bool dfpPlayFolder(uint8_t folder, uint8_t file);      // файл NNN.mp3 в корне SD (0001.mp3...)
void dfpStop();
int dfpTracks();                    // сколько файлов на карте (если известно)
