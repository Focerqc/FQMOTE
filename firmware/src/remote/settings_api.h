#pragma once
#include "cJSON.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

  // Caller frees the result with cJSON_Delete.
  cJSON *settings_describe_json(void);

  // Returns 0, or -1 with a user-facing message in error.
  int settings_apply_json(const char *json, char *error, size_t error_size);

  // These return an esp_err_t.
  int settings_save_device_preferences(void);
  int settings_save_string(const char *key, const char *value);
  struct InputPinSettings;
  int settings_save_input_pins(const struct InputPinSettings *pins);

#ifdef __cplusplus
}
#endif
