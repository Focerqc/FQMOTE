#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  bool is_settings_screen_active();
  void setup_settings_properties();
  void handle_settings_save();

#ifdef __cplusplus
}
#endif
