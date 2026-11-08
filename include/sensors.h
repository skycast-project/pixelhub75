#pragma once

// Тип реально найденного датчика (для веб-индикации и для того, чтобы
// readSensor() знала, откуда брать данные).
enum class SensorType {
  NONE,     // ничего не найдено
  BME280,   // темп + влажность + давление
  BMP280,   // темп + давление (БЕЗ влажности)
  BMP180,   // темп + давление (БЕЗ влажности, старый датчик)
  AHT10,    // темп + влажность (AHT10/AHT20)
  SHT21     // темп + влажность (SHT21/HTU21D)
};

inline bool sensorHasHumidity(SensorType t) {
  return t == SensorType::BME280 || t == SensorType::AHT10 || t == SensorType::SHT21;
}
inline bool sensorHasPressure(SensorType t) {
  return t == SensorType::BME280 || t == SensorType::BMP280 || t == SensorType::BMP180;
}

// Глобально доступный результат автоопределения - веб-интерфейс читает
// это напрямую, чтобы показать цветной кружок статуса.
extern SensorType detectedSensor;

// Автоматически перебирает датчики на шине I2C: сначала BME280
// (0x76, потом 0x77), если не нашёлся - пробует BMP280 (тоже оба
// адреса). Результат кладёт в detectedSensor и печатает в Serial.
// Вызывать один раз в setup(), после Serial.begin().
// Возвращает true, если найден хоть какой-то датчик.
bool initSensor();

// Считывает данные с ТЕМ датчиком, что реально нашёлся (detectedSensor),
// в глобальные homeTemp/homeHumidity/homePressure (globals.h).
// Если найден BMP280 - homeHumidity не трогается (у него нет влажности).
// Ничего не делает, если ничего не найдено.
void readSensor();

// Если в настройках датчиков выбран Wi-Fi датчик как «домашний»,
// подменяет показания встроенного I2C-датчика.
void applyHomeSourceOverride();
