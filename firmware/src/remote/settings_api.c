#include "settings_api.h"
#include "../config.h"
#include "cJSON.h"
#include "remoteinputs.h"
#include "settings.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Dropdown option labels. Each table is indexed by its enum value and asserted
// against that enum's _COUNT, so extending an enum without adding a label fails
// the build instead of silently shifting what the UI saves.
#define DEFINE_SETTING_OPTIONS(fn_name, table, count_sentinel)                                                         \
  _Static_assert(sizeof(table) / sizeof((table)[0]) == (count_sentinel), #table " out of sync with " #count_sentinel); \
  SettingOptions fn_name(void) {                                                                                       \
    SettingOptions options = {.labels = table, .count = sizeof(table) / sizeof((table)[0])};                           \
    return options;                                                                                                    \
  }

static const char *const DOUBLE_PRESS_LABELS[] = {"None", "Open menu"};
DEFINE_SETTING_OPTIONS(settings_double_press_options, DOUBLE_PRESS_LABELS, DOUBLE_PRESS_ACTION_COUNT)

static const char *const ROTATION_LABELS[] = {"None", "90 degrees", "180 degrees", "270 degrees"};
DEFINE_SETTING_OPTIONS(settings_rotation_options, ROTATION_LABELS, SCREEN_ROTATION_COUNT)

static const char *const AUTO_OFF_LABELS[] = {"Disabled",   "2 minutes",  "5 minutes",
                                              "10 minutes", "20 minutes", "30 minutes"};
DEFINE_SETTING_OPTIONS(settings_auto_off_options, AUTO_OFF_LABELS, AUTO_OFF_COUNT)

static const char *const TEMP_UNITS_LABELS[] = {"Celsius", "Fahrenheit"};
DEFINE_SETTING_OPTIONS(settings_temp_units_options, TEMP_UNITS_LABELS, TEMP_UNITS_COUNT)

static const char *const DISTANCE_UNITS_LABELS[] = {"Kilometers", "Miles"};
DEFINE_SETTING_OPTIONS(settings_distance_units_options, DISTANCE_UNITS_LABELS, DISTANCE_UNITS_COUNT)

static const char *const STARTUP_SOUND_LABELS[] = {"Disabled", "Beep", "Melody"};
DEFINE_SETTING_OPTIONS(settings_startup_sound_options, STARTUP_SOUND_LABELS, STARTUP_SOUND_COUNT)

// Capability-dependent options use the same labels as the on-device menu.
static SettingOptions hbm_options(void) {
  static const char *labels[HBM_MODE_COUNT];
  for (int i = 0; i < HBM_MODE_COUNT; ++i) {
    labels[i] = hbm_mode_label((HbmModeOptions)i);
  }
  return (SettingOptions){labels, display_supports_hbm() ? (IMU_ENABLED ? HBM_MODE_COUNT : 2) : 1};
}

static SettingOptions led_options(void) {
  static const char *labels[LED_MODE_COUNT];
  for (int i = 0; i < LED_MODE_COUNT; ++i) {
    labels[i] = led_mode_label((LedModeOptions)i);
  }
  return (SettingOptions){labels, led_is_supported() ? LED_MODE_COUNT : 1};
}

static const char *const BATTERY_LABELS[] = {"Percentage", "Voltage"};
DEFINE_SETTING_OPTIONS(battery_options, BATTERY_LABELS, BATTERY_DISPLAY_VOLTAGE + 1)
static const char *const SECONDARY_LABELS[] = {"Duty cycle", "Temperatures", "Distance"};
DEFINE_SETTING_OPTIONS(secondary_options, SECONDARY_LABELS, SECONDARY_STAT_DISTANCE + 1)
static const char *const POCKET_LABELS[] = {"Disabled", "Enabled"};
DEFINE_SETTING_OPTIONS(pocket_options, POCKET_LABELS, POCKET_MODE_ENABLED + 1)

// Typed accessors avoid aliasing enum and byte-sized members through integers.
#define DEFINE_DEVICE_ACCESSORS(key, member, type)                                                                     \
  static uint32_t read_##key(void) { return device_settings.member; }                                                  \
  static void apply_##key(uint32_t value) { device_settings.member = (type)value; }
DEFINE_DEVICE_ACCESSORS(bl_level, bl_level, uint8_t)
DEFINE_DEVICE_ACCESSORS(screen_rotation, screen_rotation, ScreenRotation)
DEFINE_DEVICE_ACCESSORS(theme_color, theme_color, uint32_t)
DEFINE_DEVICE_ACCESSORS(battery_display, battery_display, BoardBatteryDisplayOption)
DEFINE_DEVICE_ACCESSORS(sec_stat_disp, secondary_stat_display, SecondaryStatDisplayOption)
DEFINE_DEVICE_ACCESSORS(hbm_mode, hbm_mode, HbmModeOptions)
DEFINE_DEVICE_ACCESSORS(auto_off_time, auto_off_time, AutoOffOptions)
DEFINE_DEVICE_ACCESSORS(pocket_mode, pocket_mode, PocketModeOptions)
DEFINE_DEVICE_ACCESSORS(temp_units, temp_units, TempUnits)
DEFINE_DEVICE_ACCESSORS(distance_units, distance_units, DistanceUnits)
DEFINE_DEVICE_ACCESSORS(startup_sound, startup_sound, StartupSoundOptions)
DEFINE_DEVICE_ACCESSORS(stats_dp, double_press_action, StatsDoublePressAction)
DEFINE_DEVICE_ACCESSORS(led_mode, led_mode, LedModeOptions)

// Shared settings API: field names, presentation, validation and storage
// bindings. The tool builds its form and Zod schema from the emitted metadata.
typedef struct {
  const char *key;
  const char *label;
  const char *group;
  const char *description;
  size_t max_bytes; // nonzero for strings
  bool secret;
  char *(*read_string)(void);
  const char *length_key; // legacy NVS length key for strings
  uint32_t (*read_number)(void);
  void (*apply_number)(uint32_t value);
  SettingOptions (*options)(void);
  uint32_t minimum;
  uint32_t maximum; // bounded integer when options is NULL
  bool color;
  size_t pin_offset;
  uint64_t (*choices)(void);
} SettingDescriptor;

static uint64_t axis_choices(void) { return input_pins_adc_capable_mask() & input_pins_assignable_mask(); }

static const SettingDescriptor fields[] = {
    {.key = "wifi_ssid",
     .label = "Network name",
     .group = "Wi-Fi",
     .description = "Leave blank to keep the remote offline",
     .max_bytes = WIFI_SSID_MAX_BYTES,
     .read_string = get_wifi_ssid,
     .length_key = "wifi_ssid_l"},
    {.key = "wifi_password",
     .label = "Password",
     .group = "Wi-Fi",
     .description = "Used for over-the-air firmware updates",
     .max_bytes = WIFI_PASSWORD_MAX_BYTES,
     .secret = true,
     .read_string = get_wifi_password,
     .length_key = "wifi_key_l"},
    {.key = "js_x_gpio",
     .label = "Joystick X",
     .group = "Input pins",
     .description = "Changing this pin clears X-axis calibration",
     .pin_offset = offsetof(InputPinSettings, js_x_gpio),
     .choices = axis_choices},
    {.key = "js_y_gpio",
     .label = "Joystick Y",
     .group = "Input pins",
     .description = "Changing this pin clears Y-axis calibration",
     .pin_offset = offsetof(InputPinSettings, js_y_gpio),
     .choices = axis_choices},
    {.key = "btn1_gpio",
     .label = "Primary button",
     .group = "Input pins",
     .description = "With no button, waking from deep sleep requires a reset",
     .pin_offset = offsetof(InputPinSettings, btn1_gpio),
     .choices = input_pins_button_capable_mask},
    {.key = "btn1_level",
     .label = "Button active level",
     .group = "Input pins",
     .description = "Electrical level when the button is pressed",
     .pin_offset = offsetof(InputPinSettings, btn1_active_level)},
    {.key = "bl_level",
     .label = "Brightness",
     .group = "Display",
     .description = "Screen brightness from 10 to 255",
     .read_number = read_bl_level,
     .apply_number = apply_bl_level,
     .minimum = 10,
     .maximum = 255},
    {.key = "screen_rotation",
     .label = "Screen rotation",
     .group = "Display",
     .description = "Orientation of the display",
     .read_number = read_screen_rotation,
     .apply_number = apply_screen_rotation,
     .options = settings_rotation_options},
    {.key = "theme_color",
     .label = "Theme colour",
     .group = "Display",
     .description = "Accent colour used by the display and LEDs",
     .read_number = read_theme_color,
     .apply_number = apply_theme_color,
     .maximum = 16777215,
     .color = true},
    {.key = "battery_display",
     .label = "Board battery display",
     .group = "Display",
     .description = "Show the board battery as a percentage or voltage",
     .read_number = read_battery_display,
     .apply_number = apply_battery_display,
     .options = battery_options},
    {.key = "sec_stat_disp",
     .label = "Secondary statistic",
     .group = "Display",
     .description = "Additional information shown on the stats screen",
     .read_number = read_sec_stat_disp,
     .apply_number = apply_sec_stat_disp,
     .options = secondary_options},
    {.key = "hbm_mode",
     .label = "High brightness mode",
     .group = "Display",
     .description = "Raised mode follows the raise-to-view gesture",
     .read_number = read_hbm_mode,
     .apply_number = apply_hbm_mode,
     .options = hbm_options},
    {.key = "auto_off_time",
     .label = "Auto-off timeout",
     .group = "Power",
     .description = "Shut down after this period of inactivity",
     .read_number = read_auto_off_time,
     .apply_number = apply_auto_off_time,
     .options = settings_auto_off_options},
    {.key = "pocket_mode",
     .label = "Pocket mode",
     .group = "Power",
     .description = "Enable pocket mode",
     .read_number = read_pocket_mode,
     .apply_number = apply_pocket_mode,
     .options = pocket_options},
    {.key = "temp_units",
     .label = "Temperature units",
     .group = "Units",
     .description = "Temperature display units",
     .read_number = read_temp_units,
     .apply_number = apply_temp_units,
     .options = settings_temp_units_options},
    {.key = "distance_units",
     .label = "Distance units",
     .group = "Units",
     .description = "Distance display units",
     .read_number = read_distance_units,
     .apply_number = apply_distance_units,
     .options = settings_distance_units_options},
    {.key = "startup_sound",
     .label = "Startup sound",
     .group = "Sound",
     .description = "Sound played the next time the remote starts",
     .read_number = read_startup_sound,
     .apply_number = apply_startup_sound,
     .options = settings_startup_sound_options},
    {.key = "stats_dp",
     .label = "Double-press action",
     .group = "Buttons",
     .description = "Action when double-pressing the button on the stats screen",
     .read_number = read_stats_dp,
     .apply_number = apply_stats_dp,
     .options = settings_double_press_options},
    {.key = "led_mode",
     .label = "LED behaviour",
     .group = "LEDs",
     .description = "LED behaviour between temporary alerts and animations",
     .read_number = read_led_mode,
     .apply_number = apply_led_mode,
     .options = led_options},
};
#define FIELD_COUNT (sizeof(fields) / sizeof(fields[0]))

// Both typed firmware setters and JSON patches use this persistence path.
int settings_save_string(const char *key, const char *value) {
  if (!key || !value) {
    return ESP_ERR_INVALID_ARG;
  }
  for (size_t i = 0; i < FIELD_COUNT; ++i) {
    const SettingDescriptor *field = &fields[i];
    if (strcmp(key, field->key) != 0) {
      continue;
    }
    if (!field->max_bytes || strlen(value) > field->max_bytes) {
      return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = nvs_write_str(field->key, value);
    if (result != ESP_OK) {
      return result;
    }
    return nvs_write_int(field->length_key, (uint32_t)strlen(value));
  }
  return ESP_ERR_INVALID_ARG;
}

static int pin_value(const SettingDescriptor *field, const InputPinSettings *pins) {
  const uint8_t *value = (const uint8_t *)pins + field->pin_offset;
  return field->choices ? (int)(int8_t)*value : *value;
}

int settings_save_input_pins(const InputPinSettings *pins) {
  if (!pins) {
    return ESP_ERR_INVALID_ARG;
  }
  for (size_t i = 0; i < FIELD_COUNT; ++i) {
    if (fields[i].max_bytes || fields[i].read_number) {
      continue;
    }
    esp_err_t result = nvs_write_int(fields[i].key, (uint32_t)(int32_t)pin_value(&fields[i], pins));
    if (result != ESP_OK) {
      return result;
    }
  }
  return ESP_OK;
}

static bool allowed_number(const SettingDescriptor *field, double value) {
  if (!isfinite(value) || floor(value) != value) {
    return false;
  }
  if (field->read_number) {
    return value >= field->minimum && (field->options ? value < field->options().count : value <= field->maximum);
  }
  if (!field->choices) {
    return value == 0 || value == 1;
  }
  return value == INPUT_PIN_DISABLED || (value >= 0 && value < 64 && (field->choices() & (1ULL << (int)value)));
}

// On-device saves and JSON patches share the same keys and validation.
int settings_save_device_preferences(void) {
  for (size_t i = 0; i < FIELD_COUNT; ++i) {
    if (fields[i].read_number && !allowed_number(&fields[i], fields[i].read_number())) {
      // Unsupported optional hardware has no effect, but keep its stored mode.
      if (!((fields[i].options == hbm_options && fields[i].read_number() < HBM_MODE_COUNT) ||
            (fields[i].options == led_options && fields[i].read_number() < LED_MODE_COUNT))) {
        return ESP_ERR_INVALID_ARG;
      }
    }
  }
  for (size_t i = 0; i < FIELD_COUNT; ++i) {
    if (fields[i].read_number) {
      int result = nvs_write_int(fields[i].key, fields[i].read_number());
      if (result != ESP_OK) {
        return result;
      }
    }
  }
  return ESP_OK;
}

static bool add_option(cJSON *options, int value, const char *label) {
  cJSON *option = cJSON_CreateObject();
  if (!option) {
    return false;
  }
  if (!cJSON_AddNumberToObject(option, "value", value) || !cJSON_AddStringToObject(option, "label", label) ||
      !cJSON_AddItemToArray(options, option)) {
    cJSON_Delete(option);
    return false;
  }
  return true;
}

static cJSON *describe_field(const SettingDescriptor *field) {
  cJSON *meta = cJSON_CreateObject();
  if (!meta) {
    return NULL;
  }
  const char *type = "integer";
  if (field->max_bytes) {
    type = "string";
  }
  else if (field->read_number && !field->options) {
    type = "range";
  }
  if (!cJSON_AddStringToObject(meta, "key", field->key) || !cJSON_AddStringToObject(meta, "label", field->label) ||
      !cJSON_AddStringToObject(meta, "group", field->group) ||
      !cJSON_AddStringToObject(meta, "description", field->description) ||
      !cJSON_AddStringToObject(meta, "type", type)) {
    goto fail;
  }
  if (field->max_bytes) {
    if (!cJSON_AddNumberToObject(meta, "maxBytes", (double)field->max_bytes) ||
        !cJSON_AddBoolToObject(meta, "secret", field->secret)) {
      goto fail;
    }
  }
  else if (field->read_number && !field->options) {
    if (!cJSON_AddNumberToObject(meta, "min", field->minimum) ||
        !cJSON_AddNumberToObject(meta, "max", field->maximum) || !cJSON_AddBoolToObject(meta, "color", field->color)) {
      goto fail;
    }
  }
  else {
    cJSON *options = cJSON_AddArrayToObject(meta, "options");
    if (!options) {
      goto fail;
    }
    if (field->options) {
      SettingOptions choices = field->options();
      for (int i = 0; i < choices.count; ++i) {
        if (!add_option(options, i, choices.labels[i])) {
          goto fail;
        }
      }
    }
    else if (field->choices) {
      if (!add_option(options, INPUT_PIN_DISABLED, "Not used")) {
        goto fail;
      }
      uint64_t mask = field->choices();
      for (int pin = 0; pin < 64; ++pin) {
        if (!(mask & (1ULL << pin))) {
          continue;
        }
        char label[16];
        snprintf(label, sizeof(label), "GPIO %d", pin);
        if (!add_option(options, pin, label)) {
          goto fail;
        }
      }
    }
    else {
      if (!add_option(options, 0, "Active low") || !add_option(options, 1, "Active high")) {
        goto fail;
      }
    }
  }
  return meta;
fail:
  cJSON_Delete(meta);
  return NULL;
}

cJSON *settings_describe_json(void) {
  cJSON *reply = cJSON_CreateObject();
  if (!reply) {
    return NULL;
  }
  if (!cJSON_AddStringToObject(reply, "kind", "settings") || !cJSON_AddNumberToObject(reply, "version", 1)) {
    goto fail;
  }
  cJSON *metadata = cJSON_AddArrayToObject(reply, "fields");
  cJSON *values = cJSON_AddObjectToObject(reply, "values");
  if (!metadata || !values) {
    goto fail;
  }
  for (size_t i = 0; i < FIELD_COUNT; ++i) {
    const SettingDescriptor *field = &fields[i];
    if ((field->options == hbm_options && !display_supports_hbm()) ||
        (field->options == led_options && !led_is_supported())) {
      continue;
    }
    cJSON *meta = describe_field(field);
    if (!meta) {
      goto fail;
    }
    if (!cJSON_AddItemToArray(metadata, meta)) {
      cJSON_Delete(meta);
      goto fail;
    }
    if (field->max_bytes) {
      const char *value = field->read_string();
      if (!cJSON_AddStringToObject(values, field->key, value ? value : "")) {
        goto fail;
      }
    }
    else {
      if (!cJSON_AddNumberToObject(
              values, field->key,
              (field->read_number ? (double)field->read_number() : (double)pin_value(field, &input_pin_settings)))) {
        goto fail;
      }
    }
  }
  char warning[192] = {0};
  input_pins_warnings(&input_pin_settings, warning, sizeof(warning));
  if (!cJSON_AddStringToObject(reply, "warning", warning)) {
    goto fail;
  }
  return reply;
fail:
  cJSON_Delete(reply);
  return NULL;
}

static int settings_error(char *error, size_t error_size, const char *message) {
  if (error && error_size) {
    snprintf(error, error_size, "%s", message);
  }
  return -1;
}

int settings_apply_json(const char *json, char *error_out, size_t error_size) {
  if (error_out && error_size) {
    error_out[0] = '\0';
  }
  if (!json) {
    return settings_error(error_out, error_size, "Expected a JSON object");
  }
  if (strlen(json) >= 2048) {
    return settings_error(error_out, error_size, "Settings payload too large");
  }
  // cJSON strings are NUL-terminated: reject escaped NUL rather than silently
  // truncating a credential or a field name. Skip escaped backslashes.
  bool in_string = false;
  int depth = 0;
  for (const char *p = json; *p; ++p) {
    if (*p == '\\' && p[1]) {
      if (strncmp(p + 1, "u0000", 5) == 0) {
        return settings_error(error_out, error_size, "NUL is not allowed in settings");
      }
      ++p;
    }
    else if (*p == '"') {
      in_string = !in_string;
    }
    else if (!in_string) {
      // Only a flat object of scalar settings is supported. Bound parser stack
      // use before handing input to cJSON's recursive descent parser.
      if (*p == '[' || (*p == '{' && ++depth > 1)) {
        return settings_error(error_out, error_size, "Expected flat settings object");
      }
      if (*p == '}') {
        --depth;
      }
    }
  }
  // Require the entire argument to parse; do not accept trailing garbage.
  cJSON *patch = cJSON_ParseWithOpts(json, NULL, true);
  const char *error = NULL;
  InputPinSettings pending = input_pin_settings;
  DeviceSettings previous_preferences = device_settings;
  bool preferences_dirty = false;
  bool pins_dirty = false;
  bool seen[FIELD_COUNT] = {false};
  if (!cJSON_IsObject(patch)) {
    error = "Expected a JSON object";
  }
  cJSON *value;
  if (!error) {
    cJSON_ArrayForEach(value, patch) {
      size_t i;
      for (i = 0; i < FIELD_COUNT; ++i) {
        if (!strcmp(value->string, fields[i].key)) {
          break;
        }
      }
      if (i == FIELD_COUNT) {
        error = "Unknown setting";
        break;
      }
      if (seen[i]) {
        error = "Duplicate setting";
        break;
      }
      seen[i] = true;
      const SettingDescriptor *field = &fields[i];
      if (field->max_bytes) {
        if (!cJSON_IsString(value) || strlen(value->valuestring) > field->max_bytes) {
          error = "Invalid string or UTF-8 byte length";
          break;
        }
      }
      else {
        if (!cJSON_IsNumber(value) || !allowed_number(field, value->valuedouble)) {
          error = "Invalid setting choice";
          break;
        }
        if (field->read_number) {
          continue;
        }
        if (pin_value(field, &pending) != value->valueint) {
          pins_dirty = true;
        }
        *((uint8_t *)&pending + field->pin_offset) = (uint8_t)value->valueint;
      }
    }
  }
  char pin_error[128] = {0};
  // Validate the entire patch before any persistence or live input changes.
  if (!error && pins_dirty && input_pins_validate(&pending, pin_error, sizeof(pin_error)) != ESP_OK) {
    error = pin_error;
  }
  if (!error && pins_dirty && input_pins_apply(&pending, pin_error, sizeof(pin_error)) != ESP_OK) {
    error = pin_error[0] ? pin_error : "Failed to apply input pins";
  }
  if (!error) {
    for (size_t i = 0; i < FIELD_COUNT; ++i) {
      if (seen[i] && fields[i].read_number) {
        uint32_t value = (uint32_t)cJSON_GetObjectItemCaseSensitive(patch, fields[i].key)->valuedouble;
        if (value != fields[i].read_number()) {
          if (nvs_write_int(fields[i].key, value) != ESP_OK) {
            error = "Failed to persist settings; reload to check applied values";
            break;
          }
          fields[i].apply_number(value);
          preferences_dirty = true;
        }
      }
      if (seen[i] && fields[i].max_bytes &&
          settings_save_string(fields[i].key, cJSON_GetObjectItemCaseSensitive(patch, fields[i].key)->valuestring) !=
              ESP_OK) {
        error = "Failed to persist settings; reload to check applied values";
        break;
      }
    }
  }
  // Refresh successful changes even when a later write fails.
  if (preferences_dirty) {
    display_refresh_device_settings(&previous_preferences);
  }
  cJSON_Delete(patch);
  if (error) {
    return settings_error(error_out, error_size, error);
  }
  return 0;
}
