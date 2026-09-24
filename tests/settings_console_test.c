// Host test: use real cJSON and ESP-IDF argv parsing, stub only device I/O.
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IMU_ENABLED 1
#include "../firmware/src/remote/input_settings.h"
#include "../firmware/src/remote/settings_api.h"
#include "../firmware/src/remote/settings_types.h"
#define STICK_MIN_VAL 0
#define STICK_MAX_VAL 4095
#define STICK_MID_VAL 2048
#define STICK_DEADBAND 50
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
typedef int esp_err_t;
InputPinSettings input_pin_settings = {-1, -1, -1, 0};
DeviceSettings device_settings = {.bl_level = 200};
static bool supports_hbm = true, supports_led = true;
static bool fail_device_write;
static const char *fail_device_key;
static int device_writes;
static uint32_t device_values[13];
static bool device_present[13];
static const char *device_keys[] = {
    "bl_level",    "screen_rotation", "theme_color",    "battery_display", "sec_stat_disp", "hbm_mode", "auto_off_time",
    "pocket_mode", "temp_units",      "distance_units", "startup_sound",   "stats_dp",      "led_mode"};
bool display_supports_hbm(void) { return supports_hbm; }
bool led_is_supported(void) { return supports_led; }
const char *hbm_mode_label(HbmModeOptions mode) {
  static const char *labels[] = {"Off", "On", "Raised"};
  return labels[mode];
}
const char *led_mode_label(LedModeOptions mode) {
  static const char *labels[] = {"Off", "Solid", "Alerts"};
  return labels[mode];
}
esp_err_t nvs_read_int(const char *key, uint32_t *value) {
  for (size_t i = 0; i < 13; ++i) {
    if (!strcmp(device_keys[i], key) && device_present[i]) {
      *value = device_values[i];
      return ESP_OK;
    }
  }
  return -1;
}
static int refreshes;
static DeviceSettings previous_refresh;
void display_refresh_device_settings(const DeviceSettings *previous) {
  previous_refresh = *previous;
  ++refreshes;
}
CalibrationSettings calibration_settings = {
    .x_min = 10, .x_max = 4000, .x_center = 2000, .y_min = 20, .y_max = 3900, .y_center = 2100, .deadband = 70};
static unsigned char input_blob[128];
static size_t input_blob_size;
static bool blob_write_fails, commit_error_after_write;
static int timer_resets;
void reset_sleep_timer(void) { ++timer_resets; }
esp_err_t nvs_write_blob(const char *key, void *value, size_t size) {
  assert(!strcmp(key, "input_state") && size <= sizeof(input_blob));
  if (blob_write_fails) {
    return -1;
  }
  memcpy(input_blob, value, size);
  input_blob_size = size;
  return commit_error_after_write ? -1 : ESP_OK;
}
esp_err_t nvs_read_blob(const char *key, void *value, size_t size) {
  assert(!strcmp(key, "input_state"));
  if (!input_blob_size || input_blob_size > size) {
    return -1;
  }
  memcpy(value, input_blob, input_blob_size);
  return ESP_OK;
}
static char ssid[WIFI_SSID_MAX_BYTES + 1] = "";
static char password[WIFI_PASSWORD_MAX_BYTES + 1] = "";
static int writes, applies;
static bool fail_write, fail_apply;
static bool fail_pin_write;
static uint32_t saved_pins[4];
char *get_wifi_ssid(void) { return ssid; }
char *get_wifi_password(void) { return password; }
esp_err_t nvs_write_str(const char *key, const char *value) {
  if (fail_write) {
    return -1;
  }
  if (!strcmp(key, "wifi_ssid")) {
    strcpy(ssid, value);
  }
  else if (!strcmp(key, "wifi_password")) {
    strcpy(password, value);
  }
  else {
    assert(false);
  }
  ++writes;
  return ESP_OK;
}
esp_err_t nvs_write_int(const char *key, uint32_t value) {
  for (size_t i = 0; i < 13; ++i) {
    if (!strcmp(device_keys[i], key)) {
      if (fail_device_write || (fail_device_key && !strcmp(fail_device_key, key))) {
        return -1;
      }
      device_values[i] = value;
      device_present[i] = true;
      ++device_writes;
      return ESP_OK;
    }
  }
  if (!strcmp(key, "wifi_ssid_l") || !strcmp(key, "wifi_key_l")) {
    return ESP_OK;
  }
  if (fail_pin_write) {
    return -1;
  }
  const char *keys[] = {"js_x_gpio", "js_y_gpio", "btn1_gpio", "btn1_level"};
  for (size_t i = 0; i < 4; ++i) {
    if (!strcmp(keys[i], key)) {
      saved_pins[i] = value;
      return ESP_OK;
    }
  }
  assert(false);
  return -1;
}
uint64_t input_pins_adc_capable_mask(void) { return (1ULL << 1) | (1ULL << 2); }
uint64_t input_pins_assignable_mask(void) { return (1ULL << 1) | (1ULL << 2) | (1ULL << 3); }
uint64_t input_pins_button_capable_mask(void) { return 1ULL << 3; }
size_t input_pins_warnings(const InputPinSettings *p, char *out, size_t n) {
  (void)p;
  (void)n;
  out[0] = 0;
  return 0;
}
esp_err_t input_pins_validate(const InputPinSettings *p, char *err, size_t n) {
  (void)n;
  if (p->js_x_gpio >= 0 && p->js_x_gpio == p->js_y_gpio) {
    strcpy(err, "duplicate axis");
    return -1;
  }
  return 0;
}
esp_err_t input_pins_apply(const InputPinSettings *p, char *err, size_t n) {
  (void)err;
  (void)n;
  if (fail_apply) {
    return -1;
  }
  if (settings_save_input_pins(p) != ESP_OK) {
    return -1;
  }
  settings_reset_calibration(&calibration_settings, p->js_x_gpio != input_pin_settings.js_x_gpio,
                             p->js_y_gpio != input_pin_settings.js_y_gpio);
  input_pin_settings = *p;
  ++applies;
  return 0;
}
#include "remote/input_settings.c"
#include "remote/settings_api.c"
#include "remote/settings_console.c"
size_t esp_console_split_argv(char *line, char **argv, size_t argv_size);

static int allocation_attempts, fail_allocation_at, outstanding_allocations;

static void *test_allocate(size_t size) {
  if (allocation_attempts++ == fail_allocation_at) {
    return NULL;
  }
  void *memory = malloc(size);
  if (memory) {
    ++outstanding_allocations;
  }
  return memory;
}

static void test_free(void *memory) {
  if (memory) {
    --outstanding_allocations;
  }
  free(memory);
}

static void test_allocation_failures(void) {
  cJSON_Hooks hooks = {.malloc_fn = test_allocate, .free_fn = test_free};
  cJSON_InitHooks(&hooks);
  fail_allocation_at = -1;
  allocation_attempts = 0;
  cJSON *metadata = settings_describe_json();
  assert(metadata);
  int metadata_allocations = allocation_attempts;
  cJSON_Delete(metadata);
  assert(outstanding_allocations == 0);
  for (int i = 0; i < metadata_allocations; ++i) {
    fail_allocation_at = i;
    allocation_attempts = 0;
    // Never return a partial schema that could silently hide settings.
    assert(settings_describe_json() == NULL);
    assert(outstanding_allocations == 0);
  }
  fail_allocation_at = -1;
  allocation_attempts = 0;
  char error[128];
  assert(settings_apply_json("{\"wifi_ssid\":\"allocation test\"}", error, sizeof(error)) == 0);
  int parse_allocations = allocation_attempts;
  int previous_writes = writes;
  for (int i = 0; i < parse_allocations; ++i) {
    fail_allocation_at = i;
    allocation_attempts = 0;
    assert(settings_apply_json("{\"wifi_ssid\":\"allocation test\"}", error, sizeof(error)) != 0);
    assert(writes == previous_writes);
    assert(outstanding_allocations == 0);
  }
  cJSON_InitHooks(NULL);
}

static int save(const char *json) {
  char *args[] = {"save_settings", (char *)json};
  return console_save_settings(2, args);
}

static void test_input_record(void) {
  calibration_settings.x_center = 1900;
  calibration_settings.y_center = 2200;
  assert(settings_store_input_state(&input_pin_settings, &calibration_settings) == ESP_OK);
  InputPinSettings next = input_pin_settings;
  next.js_x_gpio = -1;
  InputPinSettings loaded;
  CalibrationSettings calibration;
  blob_write_fails = true;
  assert(settings_save_input_pins(&next) != ESP_OK);
  blob_write_fails = false;
  assert(settings_load_input_state(&loaded, &calibration) == ESP_OK);
  assert(loaded.js_x_gpio == 1 && calibration.x_center == 1900);
  // A commit error can leave the new blob durable. It must still be a complete pair.
  commit_error_after_write = true;
  assert(settings_save_input_pins(&next) != ESP_OK);
  commit_error_after_write = false;
  assert(input_pin_settings.js_x_gpio == 1 && calibration_settings.x_center == 1900);
  assert(settings_load_input_state(&loaded, &calibration) == ESP_OK);
  assert(loaded.js_x_gpio == -1 && calibration.x_center == STICK_MID_VAL);
  assert(calibration.y_center == 2200 && calibration.deadband == STICK_DEADBAND);
  // Truncated or future records must not overwrite the caller's legacy defaults.
  input_blob_size = 4;
  loaded.js_x_gpio = 42;
  assert(settings_load_input_state(&loaded, &calibration) != ESP_OK && loaded.js_x_gpio == 42);
  assert(settings_store_input_state(&input_pin_settings, &calibration_settings) == ESP_OK);
  input_blob[0] = 99;
  assert(settings_load_input_state(&loaded, &calibration) != ESP_OK && loaded.js_x_gpio == 42);
}

static void test_calibration_needed(void) {
  InputPinSettings pins = {.js_x_gpio = 1, .js_y_gpio = 2, .btn1_gpio = 3};
  CalibrationSettings calibration = {
      .x_min = 10, .x_max = 4000, .x_center = 2000, .y_min = 20, .y_max = 3900, .y_center = 2100};
  assert(!settings_calibration_needed(&pins, &calibration));
  settings_reset_calibration(&calibration, false, true);
  assert(settings_calibration_needed(&pins, &calibration));
  pins.js_y_gpio = INPUT_PIN_DISABLED;
  assert(!settings_calibration_needed(&pins, &calibration));
  settings_reset_calibration(&calibration, true, false);
  assert(settings_calibration_needed(&pins, &calibration));
}

static void test_device_preferences(void) {
  assert(settings_save_device_preferences() == ESP_OK);
  assert(device_writes == 13);
  assert(save("{\"bl_level\":10,\"screen_rotation\":3,\"theme_color\":16777215,\"battery_display\":1,\"sec_stat_disp\":"
              "2,\"hbm_mode\":2,\"auto_off_time\":5,\"pocket_mode\":1,\"temp_units\":1,\"distance_units\":1,\"startup_"
              "sound\":2,\"stats_dp\":1,\"led_mode\":2}") == 0);
  assert(device_writes == 26);
  assert(timer_resets == 1);
  assert(device_settings.bl_level == 10 && device_settings.theme_color == 16777215);
  assert(refreshes == 1 && previous_refresh.bl_level == 200);
  assert(save("{\"bl_level\":10}") == 0 && device_writes == 26 && refreshes == 1);
  cJSON *metadata = settings_describe_json();
  assert(metadata);
  cJSON *values = cJSON_GetObjectItemCaseSensitive(metadata, "values");
  assert(cJSON_GetObjectItemCaseSensitive(values, "bl_level")->valueint == 10);
  assert(cJSON_GetObjectItemCaseSensitive(values, "theme_color")->valueint == 16777215);
  cJSON_Delete(metadata);
  const char *bad[] = {"{\"bl_level\":9}",
                       "{\"bl_level\":256}",
                       "{\"bl_level\":10.5}",
                       "{\"theme_color\":-1}",
                       "{\"theme_color\":16777216}",
                       "{\"screen_rotation\":4}",
                       "{\"auto_off_time\":6}",
                       "{\"pocket_mode\":2}",
                       "{\"temp_units\":2}",
                       "{\"distance_units\":2}",
                       "{\"startup_sound\":3}",
                       "{\"stats_dp\":2}",
                       "{\"hbm_mode\":3}",
                       "{\"led_mode\":3}",
                       "{\"battery_display\":2}",
                       "{\"sec_stat_disp\":3}",
                       "{\"bl_level\":20,\"wifi_ssid\":false}"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
    assert(save(bad[i]) != 0);
    assert(device_writes == 26);
  }
  fail_device_write = true;
  assert(save("{\"bl_level\":255}") != 0);
  assert(settings_save_device_preferences() != ESP_OK);
  fail_device_write = false;
  assert(device_settings.bl_level == 10 && refreshes == 1);
  fail_device_key = "screen_rotation";
  assert(save("{\"bl_level\":255,\"screen_rotation\":1}") != 0);
  assert(device_settings.bl_level == 255 && device_settings.screen_rotation == 3);
  assert(device_values[0] == 255 && device_values[1] == 3);
  assert(refreshes == 2 && previous_refresh.bl_level == 10);
  fail_device_key = NULL;
  supports_hbm = supports_led = false;
  assert(save("{\"hbm_mode\":1}") != 0);
  assert(save("{\"led_mode\":1}") != 0);
  metadata = settings_describe_json();
  assert(metadata);
  values = cJSON_GetObjectItemCaseSensitive(metadata, "values");
  assert(!cJSON_GetObjectItemCaseSensitive(values, "hbm_mode"));
  assert(!cJSON_GetObjectItemCaseSensitive(values, "led_mode"));
  cJSON_Delete(metadata);
  supports_hbm = supports_led = true;
  device_settings.bl_level = 9;
  assert(settings_save_device_preferences() != ESP_OK);
  device_settings.bl_level = 200;
}

int main(int argc, char **argv) {
  // Optional mode permits the JS suite to consume actual firmware metadata.
  if (argc > 1 && !strcmp(argv[1], "metadata")) {
    return console_get_settings(1, argv);
  }
  if (argc > 1 && !strcmp(argv[1], "command")) {
    char line[2048], *args[4];
    if (!fgets(line, sizeof(line), stdin)) {
      return 1;
    }
    line[strcspn(line, "\r\n")] = 0;
    int count = (int)esp_console_split_argv(line, args, 4);
    if (console_save_settings(count, args) != 0) {
      return 1;
    }
    char *get[] = {"settings", count == 3 ? args[2] : NULL};
    return console_get_settings(count == 3 ? 2 : 1, get);
  }
  assert(save("{\"wifi_ssid\":\"hello\",\"js_x_gpio\":1,\"js_y_gpio\":2}") == 0);
  assert(writes == 1 && applies == 1 && !strcmp(ssid, "hello"));
  assert(save("{\"js_x_gpio\":1}") == 0 && applies == 1);
  const char *bad[] = {
      "{} garbage",
      "[]",
      "null",
      "{",
      "{\"unknown\":1}",
      "{\"wifi_ssid\":{\"nested\":{}}}",
      "{\"wifi_ssid\":[[[[]]]]}",
      "{\"wifi_ssid\":true}",
      "{\"js_x_gpio\":1.5}",
      "{\"js_x_gpio\":\"1\"}",
      "{\"js_x_gpio\":64}",
      "{\"js_x_gpio\":-2}",
      "{\"js_x_gpio\":3}",
      "{\"btn1_gpio\":1}",
      "{\"btn1_level\":2}",
      "{\"wifi_ssid\":\"123456789012345678901234567890123\"}",
      "{\"wifi_ssid\":\"bad\\u0000suffix\"}",
      "{\"wifi_ssid\\u0000suffix\":\"bad\"}",
      "{\"wifi_ssid\":\"changed\",\"js_y_gpio\":1}",
      "{\"wifi_ssid\":\"first\",\"wifi_ssid\":\"duplicate\"}",
  };
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
    assert(save(bad[i]) != 0);
    assert(writes == 1 && applies == 1 && !strcmp(ssid, "hello"));
  }
  assert(save("{\"wifi_password\":\"literal \\\\u0000\"}") == 0);
  assert(!strcmp(password, "literal \\u0000"));
  char line[] = "save_settings \"{\\\"wifi_ssid\\\":\\\"  a \\\\\\\"b\\\\\\\" \\\\\\\\ c  \\\"}\"";
  char *args[4];
  size_t count = esp_console_split_argv(line, args, 4);
  assert(count == 2 && console_save_settings((int)count, args) == 0);
  assert(!strcmp(ssid, "  a \"b\" \\ c  "));
  char *tagged[] = {"save_settings", "{\"wifi_ssid\":\"tagged\"}", "r1-a_B"};
  assert(console_save_settings(3, tagged) == 0 && !strcmp(ssid, "tagged"));
  char *get_tagged[] = {"settings", "r1"};
  assert(console_get_settings(2, get_tagged) == 0);
  const char *bad_ids[] = {"", "has space", "quo\"te", "123456789012345678901234567890123"};
  for (size_t i = 0; i < sizeof(bad_ids) / sizeof(bad_ids[0]); ++i) {
    char *bad_save[] = {"save_settings", "{\"wifi_ssid\":\"rejected\"}", (char *)bad_ids[i]};
    char *bad_get[] = {"settings", (char *)bad_ids[i]};
    assert(console_save_settings(3, bad_save) != 0 && !strcmp(ssid, "tagged"));
    assert(console_get_settings(2, bad_get) != 0);
  }
  fail_write = true;
  assert(save("{\"wifi_ssid\":\"failure\"}") != 0);
  fail_write = false;
  fail_apply = true;
  int previous_writes = writes;
  assert(save("{\"wifi_ssid\":\"unchanged\",\"js_x_gpio\":-1}") != 0);
  assert(writes == previous_writes);
  assert(settings_save_string("wifi_ssid", "typed save") == ESP_OK);
  assert(!strcmp(ssid, "typed save"));
  assert(settings_save_string("wifi_ssid", "123456789012345678901234567890123") != ESP_OK);
  assert(settings_save_string("unknown", "value") != ESP_OK);
  assert(settings_save_string("js_x_gpio", "1") != ESP_OK);
  assert(settings_save_string("wifi_ssid", NULL) != ESP_OK);
  assert(settings_save_input_pins(&input_pin_settings) == ESP_OK);
  InputPinSettings loaded_pins;
  CalibrationSettings loaded_calibration;
  assert(settings_load_input_state(&loaded_pins, &loaded_calibration) == ESP_OK);
  assert(!memcmp(&loaded_pins, &input_pin_settings, sizeof(loaded_pins)));
  blob_write_fails = true;
  assert(settings_save_input_pins(&input_pin_settings) != ESP_OK);
  blob_write_fails = false;
  test_input_record();
  test_calibration_needed();
  test_device_preferences();
  test_allocation_failures();
  puts("settings console tests passed");
  return 0;
}
