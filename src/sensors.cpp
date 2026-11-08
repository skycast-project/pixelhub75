#include "sensors.h"
#include "local_sensors.h"
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_BMP085.h>
#include <Adafruit_AHTX0.h>
#include "globals.h"
#include "pins.h"

static Adafruit_BME280 bme;
static Adafruit_BMP280 bmp;
static Adafruit_BMP085 bmp180;
static Adafruit_AHTX0 aht;

// SHT21/HTU21D: простой I2C-обмен без библиотеки
static float shtRead(uint8_t cmd) {
  Wire.beginTransmission(0x40);
  Wire.write(cmd);
  if (Wire.endTransmission() != 0) return NAN;
  delay(60);
  if (Wire.requestFrom(0x40, 3) != 3) return NAN;
  uint16_t v = (Wire.read() << 8) | Wire.read();
  Wire.read();   // CRC не проверяем
  if (cmd == 0xE3) return -46.85f + 175.72f * (float)v / 65536.0f;   // температура
  return -6.0f + 125.0f * (float)v / 65536.0f;                        // влажность
}

SensorType detectedSensor = SensorType::NONE;

bool initSensor() {
  Wire.begin(SENSOR_SDA_PIN, SENSOR_SCL_PIN);

  Serial.println("🔍 Ищем датчик на шине I2C (BME280, затем BMP280)...");

  if (bme.begin(0x76) || bme.begin(0x77)) {
    detectedSensor = SensorType::BME280;
    Serial.println("✅ Найден BME280 (темп+влажность+давление)");
    return true;
  }

  if (bmp.begin(0x76) || bmp.begin(0x77)) {
    detectedSensor = SensorType::BMP280;
    Serial.println("✅ Найден BMP280 (темп+давление, БЕЗ влажности)");
    return true;
  }

  if (bmp180.begin(0x77)) {
    detectedSensor = SensorType::BMP180;
    Serial.println("✅ Найден BMP180/BMP085 (темп+давление, БЕЗ влажности)");
    return true;
  }

  aht.begin(&Wire);
  {
    sensors_event_t hum, temp;
    if (aht.getEvent(&hum, &temp) && !isnan(temp.temperature)) {
      detectedSensor = SensorType::AHT10;
      Serial.println("✅ Найден AHT10/AHT20 (темп+влажность)");
      return true;
    }
  }

  {
    float t = shtRead(0xE3);
    if (!isnan(t) && t > -50 && t < 90) {
      detectedSensor = SensorType::SHT21;
      Serial.println("✅ Найден SHT21/HTU21D (темп+влажность)");
      return true;
    }
  }

  detectedSensor = SensorType::NONE;
  Serial.println("❌ Датчик не найден (проверены оба адреса для BME280 и BMP280)");
  Serial.println("   Проверьте подключение SDA/SCL и питание (3.3V, не 5V!)");
  return false;
}

void readSensor() {
  switch (detectedSensor) {
    case SensorType::BME280:
      homeTemp = bme.readTemperature() + homeTempOffset;           // °C
      homeHumidity = constrain(bme.readHumidity() + homeHumidityOffset, 0.0f, 100.0f);           // %
      homePressure = bme.readPressure() / 100.0F + homePressureOffsetMm / 0.750062F;  // гПа
      break;
    case SensorType::BMP280:
      homeTemp = bmp.readTemperature() + homeTempOffset;            // °C
      // homeHumidity не трогаем - у BMP280 нет датчика влажности,
      // на экране/веб для него нужно показывать "н/д" (см. globals.h)
      homePressure = bmp.readPressure() / 100.0F + homePressureOffsetMm / 0.750062F;  // гПа
      break;
    case SensorType::BMP180:
      homeTemp = bmp180.readTemperature() + homeTempOffset;
      homePressure = bmp180.readPressure() / 100.0F + homePressureOffsetMm / 0.750062F;
      break;
    case SensorType::AHT10: {
      sensors_event_t hum, temp;
      if (aht.getEvent(&hum, &temp)) {
        homeTemp = temp.temperature + homeTempOffset;
        homeHumidity = constrain(hum.relative_humidity + homeHumidityOffset, 0.0f, 100.0f);
      }
      break;
    }
    case SensorType::SHT21: {
      float t = shtRead(0xE3);
      float h = shtRead(0xE5);
      if (!isnan(t)) homeTemp = t + homeTempOffset;
      if (!isnan(h)) homeHumidity = constrain(h + homeHumidityOffset, 0.0f, 100.0f);
      break;
    }
    case SensorType::NONE:
    default:
      break; // датчика нет - значения остаются как были (0 по умолчанию)
  }
}

// Если на странице датчиков выбран Wi-Fi датчик как «домашний» — подменяем
// показания встроенного I2C-датчика его значениями.
void applyHomeSourceOverride() {
  static bool s_fromWifi = false;
  float wt, wh, wp;
  if (!localSensorsHome(wt, wh, wp)) {
    // Данных нет - оставляем последние значения (экран не должен мерцать).
    // Но если источник «Дома» вообще не выбран - возвращаемся к I2C.
    if (!localSensorsHomeConfigured()) homeSourceActive = false;
    return;
  }
  if (!isnan(wt)) homeTemp = wt;
  if (!isnan(wh)) homeHumidity = wh;
  if (!isnan(wp)) homePressure = wp;
  // Встроенного датчика нет, но значения есть - делаем вид, что датчик найден,
  // иначе экран «Дома» прячет температуру и давление.
  homeSourceActive = true;   // экран «Дома» и строку берём из Wi-Fi датчика
  (void)s_fromWifi;
}
