#include "settings_console.h"
#include "settings_api.h"
#include <stdbool.h>
#include <stdio.h>

// The console knows only command framing. All setting definitions, validation,
// persistence and live application belong to the shared settings API.
static int reply_error(const char *message) {
  cJSON *reply = cJSON_CreateObject();
  char *text = NULL;
  if (reply && cJSON_AddStringToObject(reply, "kind", "settings_result") &&
      cJSON_AddNumberToObject(reply, "version", 1) && cJSON_AddBoolToObject(reply, "ok", false) &&
      cJSON_AddStringToObject(reply, "error", message)) {
    text = cJSON_PrintUnformatted(reply);
  }
  if (text) {
    puts(text);
    cJSON_free(text);
  }
  else {
    // A static frame still reports failure when the JSON allocator is exhausted.
    puts("{\"kind\":\"settings_result\",\"version\":1,\"ok\":false,\"error\":\"Out of memory\"}");
  }
  cJSON_Delete(reply);
  return -1;
}

int console_get_settings(int argc, char **argv) {
  (void)argv;
  if (argc != 1) {
    return reply_error("Usage: settings");
  }
  cJSON *reply = settings_describe_json();
  char *text = cJSON_PrintUnformatted(reply);
  cJSON_Delete(reply);
  if (!text) {
    return reply_error("Unable to allocate settings response");
  }
  puts(text);
  cJSON_free(text);
  return 0;
}

int console_save_settings(int argc, char **argv) {
  if (argc != 2) {
    return reply_error("Expected one quoted JSON object");
  }
  char error[128];
  if (settings_apply_json(argv[1], error, sizeof(error)) != 0) {
    return reply_error(error);
  }
  puts("{\"kind\":\"settings_result\",\"version\":1,\"ok\":true}");
  return 0;
}
