#include "watchdog.h"
#include <esp_task_wdt.h>

// API esp_task_wdt отличается между старым (arduino-esp32 2.x / IDF4)
// и новым (arduino-esp32 3.x / IDF5) ядром. Поддерживаем оба варианта.
// В новом ядре watchdog иногда уже инициализирован самим Arduino-core
// (по умолчанию из sdkconfig) - тогда повторный esp_task_wdt_init()
// вернёт ESP_ERR_INVALID_STATE, и нужно использовать reconfigure().
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5

void initWatchdog(uint32_t timeoutSeconds) {
  esp_task_wdt_config_t wdtConfig = {};
  wdtConfig.timeout_ms = timeoutSeconds * 1000UL;
  wdtConfig.idle_core_mask = 0;
  wdtConfig.trigger_panic = true;

  esp_err_t err = esp_task_wdt_init(&wdtConfig);
  if (err == ESP_ERR_INVALID_STATE) {
    // watchdog уже был запущен ядром - просто меняем таймаут
    esp_task_wdt_reconfigure(&wdtConfig);
  }
  esp_task_wdt_add(NULL);
  Serial.printf("🐕 Watchdog включён (таймаут %u сек)\n", (unsigned)timeoutSeconds);
}

#else

void initWatchdog(uint32_t timeoutSeconds) {
  esp_task_wdt_init(timeoutSeconds, true);
  esp_task_wdt_add(NULL);
  Serial.printf("🐕 Watchdog включён (таймаут %u сек)\n", (unsigned)timeoutSeconds);
}

#endif

void feedWatchdog() {
  esp_task_wdt_reset();
}
