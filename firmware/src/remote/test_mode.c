#include "config.h"

#if TEST_MODE

  #include "esp_log.h"
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "remote/connection.h"
  #include "remote/stats.h"
  #include "remote/test_mode.h"
  #include "remote/time.h"
  #include <math.h>

static const char *TAG = "TEST_MODE";

typedef enum {
  MOCK_RAMP_UP,
  MOCK_HANG_HIGH,
  MOCK_RAMP_DOWN,
  MOCK_HANG_LOW
} MockState;

static void test_mode_task(void *pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(5000));
  connection_update_state(CONNECTION_STATE_CONNECTED);

  MockState state = MOCK_RAMP_UP;
  float mock_duty = 0.0f;
  uint32_t hang_counter = 0;

  while (1) {
    switch (state) {
    case MOCK_RAMP_UP:
      mock_duty += 0.21f;
      if (mock_duty >= 90.0f) {
        mock_duty = 90.0f;
        state = MOCK_HANG_HIGH;
        hang_counter = 120; // Hang around 90% for ~1.8 seconds (120 * 15ms)
      }
      break;

    case MOCK_HANG_HIGH:
      // Float naturally around 88.5% - 91.5% duty (~49 - 51 mph)
      mock_duty = 90.0f + sinf((float)hang_counter * 0.08f) * 1.5f;
      if (hang_counter > 0) {
        hang_counter--;
      }
      else {
        state = MOCK_RAMP_DOWN;
      }
      break;

    case MOCK_RAMP_DOWN:
      mock_duty -= 0.21f;
      if (mock_duty <= 0.0f) {
        mock_duty = 0.0f;
        state = MOCK_HANG_LOW;
        hang_counter = 40; // Brief pause at 0 for ~0.6 seconds
      }
      break;

    case MOCK_HANG_LOW:
      mock_duty = 0.0f;
      if (hang_counter > 0) {
        hang_counter--;
      }
      else {
        state = MOCK_RAMP_UP;
      }
      break;
    }

    remoteStats.dutyCycle = (uint8_t)roundf(mock_duty);
    // Ratio so that at ~90% duty, converted speed in MPH is ~48-50 mph:
    // 50.0 mph / 0.621371 = 80.467 km/h -> 80.467 / 90.0 = 0.894 km/h per % duty
    remoteStats.speed = mock_duty * 0.894f;
    remoteStats.batteryPercentage = 80;
    remoteStats.batteryVoltage = 74.0f;
    remoteStats.switchState = SWITCH_STATE_BOTH;
    remoteStats.motorTemp = 40.0f;
    remoteStats.controllerTemp = 35.0f;
    remoteStats.tripDistance += 10.0f;
    remoteStats.remoteBatteryPercentage = 95;

    // Mock signal strength (RSSI) so RSSI arcs render correctly
    remoteStats.signalStrength = -55; // RSSI_GOOD is -75, so -55 shows 3 bars

    // Mock battery charging state, cycling every 10 seconds (200 ticks of 50ms)
    static uint32_t tick_count = 0;
    tick_count++;
    if ((tick_count / 200) % 2 == 0) {
      remoteStats.chargeState = CHARGE_STATE_CHARGING;
    }
    else {
      remoteStats.chargeState = CHARGE_STATE_NOT_CHARGING;
    }

    remoteStats.lastUpdated = get_current_time_ms();

    stats_update();

    vTaskDelay(pdMS_TO_TICKS(15));
  }
}

void test_mode_init(void) {
  ESP_LOGI(TAG, "Initializing test mode...");
  BaseType_t ret = xTaskCreate(test_mode_task, "test_mode_task", 4096, NULL, 5, NULL);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create test_mode_task! error: %d", (int)ret);
  }
  else {
    ESP_LOGI(TAG, "test_mode_task created successfully");
  }
}

#endif
