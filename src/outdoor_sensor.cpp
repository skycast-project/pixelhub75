
#include "outdoor_sensor.h"
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>
#include "globals.h"
#include "pins.h"

static OneWire *oneWire = nullptr;
static DallasTemperature *ds = nullptr;

static bool present = false;              // датчик найден на шине
static bool conversionRequested = false;  // запрос температуры отправлен, ждём результат
static unsigned long requestedAt = 0;
static unsigned long lastCycleAt = 0;
static int badReads = 0;                  // подряд плохих чтений
static DeviceAddress sensorAddr;         // адрес датчика, запоминаем один раз
static bool haveAddr = false;

static const unsigned long CONVERSION_MS = 800;    // 12 бит: преобразование до ~750 мс
static const unsigned long PERIOD_MS = 10000;      // как часто опрашивать
static const unsigned long ABSENT_PERIOD_MS = 30000;  // как часто искать датчик, пока его нет
static const int BAD_READS_TO_INVALID = 3;         // сколько плохих чтений подряд = повторный поиск датчика на шине

// ----- УСТАРЕВАНИЕ ПОКАЗАНИЙ <-- НАСТРОИТЬ -----
// Если за это время не было ни одного удачного чтения, датчик считается
// потерянным: outdoorTempValid = false (на ночном экране прочерки "--",
// строка "Сейчас на улице" пропускается, в MQTT/Народмон не уходит).
static const unsigned long OUTDOOR_STALE_MS = 10UL * 60UL * 1000UL;   // 10 минут

// Необязательно: считать датчик потерянным, если ЗНАЧЕНИЕ не менялось столько
// времени (для радиоприёмника, который при потере передатчика может отдавать
// последнее значение как ни в чём не бывало). 0 = выключено.
// У настоящего датчика температура может долго не меняться (особенно ночью),
// поэтому включайте с большим запасом, например 3UL * 60UL * 60UL * 1000UL (3 часа).
static const unsigned long OUTDOOR_UNCHANGED_MS = 0;

static bool haveGood = false;             // было ли хоть одно удачное чтение
static unsigned long lastGoodAt = 0;      // когда было последнее удачное чтение
static float lastChangedValue = 0;        // значение, с которого отсчитываем "не менялось"
static unsigned long lastChangeAt = 0;    // когда значение последний раз менялось

// Быстрая проверка: есть ли на шине хоть один датчик (~1 мс, если никого нет).
// Нужна потому, что DallasTemperature::begin() при ПУСТОЙ шине делает
// несколько повторных попыток с delay(100) - то есть блокирует loop() на
// ~300 мс каждый раз. Раньше это случалось каждые 10 с, пока датчик не
// подключён - это и были замирания бегущей строки.
static bool busHasDevice() {
  uint8_t addr[8];
  oneWire->reset_search();
  bool found = oneWire->search(addr);
  oneWire->reset_search();
  return found;
}

static void rescan() {
  present = false;
  haveAddr = false;
  if (!busHasDevice()) return;   // датчика нет - быстро выходим, ds->begin() не зовём
  ds->begin();
  present = ds->getDeviceCount() > 0;
  // Запоминаем адрес: getTempCByIndex() каждый раз заново ищет ROM на шине
  // (~15-20 мс с отключёнными прерываниями - заметный подвис анимации).
  haveAddr = present && ds->getAddress(sensorAddr, 0);
  if (present && !haveAddr) present = false;
}

bool initOutdoorSensor() {
  if (!oneWire) {
    oneWire = new OneWire(outdoorPin);
    ds = new DallasTemperature(oneWire);
    Serial.printf("🌡 DS18B20: DATA = GPIO %d\n", outdoorPin);
  }
  rescan();
  ds->setWaitForConversion(false);   // не блокировать loop() на время преобразования
  if (present) {
    Serial.println("✅ Найден уличный датчик DS18B20");
  } else {
    Serial.println("ℹ️ Уличный датчик DS18B20 не найден (будет повторный поиск; пин GPIO" +
                   String(OUTDOOR_SENSOR_PIN) + ", подтяжка 4.7 кОм, питание 3.3V)");
  }
  return present;
}

// Нормальное ли значение. -127 = датчик не отвечает; 85.0 = значение
// "после включения", преобразование ещё не выполнялось.
static bool saneValue(float t) {
  if (t == DEVICE_DISCONNECTED_C) return false;
  if (t == 85.0f) return false;
  if (t < -55.0f || t > 125.0f) return false;
  return true;
}

// Пересчитывает outdoorTempValid по времени последнего удачного чтения
// (и, если включено, по времени последнего изменения значения).
static void updateValidity(unsigned long now) {
  bool ok = haveGood && (now - lastGoodAt <= OUTDOOR_STALE_MS);
  if (ok && OUTDOOR_UNCHANGED_MS > 0 && (now - lastChangeAt > OUTDOOR_UNCHANGED_MS)) ok = false;
  if (outdoorTempValid && !ok) {
    Serial.println("⚠️ Уличный датчик: показания устарели - данных нет");
  }
  outdoorTempValid = ok;
}

void readOutdoorSensor() {
  unsigned long now = millis();
  updateValidity(now);   // проверяем устаревание при каждом вызове (раз в секунду)

  // 1) Ждём окончания преобразования и забираем результат
  if (conversionRequested) {
    if (now - requestedAt < CONVERSION_MS) return;
    conversionRequested = false;

    float t = haveAddr ? ds->getTempC(sensorAddr) : DEVICE_DISCONNECTED_C;
    if (saneValue(t)) {
      if (!outdoorTempValid) {
        Serial.printf("🌡️ Уличный датчик: %.1f°C\n", t);
      }
      outdoorTemp = t;
      badReads = 0;
      lastGoodAt = now;
      if (!haveGood || fabsf(t - lastChangedValue) > 0.05f) {
        lastChangedValue = t;
        lastChangeAt = now;
      }
      haveGood = true;
      updateValidity(now);
    } else {
      badReads++;
      // Само "данных нет" теперь решает таймер устаревания (OUTDOOR_STALE_MS),
      // а не число плохих чтений. Здесь только запускаем повторный поиск датчика.
      if (badReads >= BAD_READS_TO_INVALID) {
        present = false;   // на следующем цикле заново поищем датчик на шине
      }
    }
    return;
  }

  // 2) Пора начинать новый цикл?
  if (lastCycleAt != 0 && now - lastCycleAt < (present ? PERIOD_MS : ABSENT_PERIOD_MS)) return;
  lastCycleAt = now;

  if (!present) {
    rescan();
    ds->setWaitForConversion(false);
    if (present) Serial.println("✅ Найден уличный датчик DS18B20");
  }
  if (present) {
    ds->requestTemperatures();   // не блокирует (setWaitForConversion(false))
    requestedAt = now;
    conversionRequested = true;
  }
}