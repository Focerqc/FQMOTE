#include "sleep_timer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "settings.h"
#include <assert.h>

static const char *TAG = "PUBREMOTE-SLEEP_TIMER";
static esp_timer_handle_t sleep_timer;
static SemaphoreHandle_t timer_mutex;
static int64_t deadline_us;
static void (*expire_callback)(void);

static void sleep_timer_callback(void *arg) {
  (void)arg;
  if (xSemaphoreTake(timer_mutex, portMAX_DELAY) == pdTRUE) {
    // A callback may already be queued when the timer is cancelled/rearmed.
    if (deadline_us && get_auto_off_ms() && esp_timer_get_time() >= deadline_us) {
      deadline_us = 0;
      expire_callback();
    }
    xSemaphoreGive(timer_mutex);
  }
}

void sleep_timer_init(void (*on_expire)(void)) {
  timer_mutex = xSemaphoreCreateMutex();
  assert(timer_mutex && on_expire);
  expire_callback = on_expire;
}

void reset_sleep_timer(void) {
  if (!timer_mutex || xSemaphoreTake(timer_mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }
  uint64_t duration_ms = get_auto_off_ms();
  deadline_us = 0;
  if (sleep_timer && esp_timer_is_active(sleep_timer)) {
    // Stop can race expiry; deadline_us guards the queued callback.
    esp_err_t result = esp_timer_stop(sleep_timer);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
      ESP_ERROR_CHECK(result);
    }
  }
  if (duration_ms) {
    if (!sleep_timer) {
      esp_timer_create_args_t args = {
          .callback = sleep_timer_callback, .dispatch_method = ESP_TIMER_TASK, .name = "SleepTimer"};
      ESP_ERROR_CHECK(esp_timer_create(&args, &sleep_timer));
    }
    deadline_us = esp_timer_get_time() + duration_ms * 1000;
    ESP_ERROR_CHECK(esp_timer_start_once(sleep_timer, duration_ms * 1000));
  }
  ESP_LOGD(TAG, "Auto-off timer updated: %llu ms", (unsigned long long)duration_ms);
  xSemaphoreGive(timer_mutex);
}
