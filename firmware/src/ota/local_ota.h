#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*local_ota_progress_cb_t)(int percent, int received_bytes, int total_bytes);
typedef void (*local_ota_status_cb_t)(const char *status);

/**
 * @brief Start the local HTTP OTA server on port 80.
 *
 * Provides:
 *   - GET /       : Modern web flasher UI
 *   - POST /update: Binary firmware stream upload endpoint
 *
 * @param progress_cb Callback called periodically during firmware upload
 * @param status_cb   Callback called on status changes or completion
 * @return esp_err_t  ESP_OK on success
 */
esp_err_t local_ota_start(local_ota_progress_cb_t progress_cb, local_ota_status_cb_t status_cb);

/**
 * @brief Stop the local HTTP OTA server.
 */
esp_err_t local_ota_stop(void);

/**
 * @brief Check if the local OTA server is active.
 */
bool local_ota_is_running(void);

#ifdef __cplusplus
}
#endif
