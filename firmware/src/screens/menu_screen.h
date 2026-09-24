#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  bool is_menu_screen_active();
  void setup_menu_properties();
  void teardown_menu_properties();

#ifdef __cplusplus
}
#endif
