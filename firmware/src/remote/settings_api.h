#pragma once
#include "cJSON.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Caller owns the returned metadata/value object and frees it with cJSON_Delete.
cJSON *settings_describe_json(void);

// Validate a complete patch, persist it and apply live changes. Returns zero on
// success, or -1 and a user-facing error. Does not print console responses.
int settings_apply_json(const char *json, char *error, size_t error_size);

// Typed firmware setters share the descriptor validation and persistence path.
// Returns an esp_err_t-compatible status (ESP_OK on success).
// Persist current on-device preferences.
int settings_save_device_preferences(void);

int settings_save_string(const char *key, const char *value);

struct InputPinSettings;
int settings_save_input_pins(const struct InputPinSettings *pins);

#ifdef __cplusplus
}
#endif
