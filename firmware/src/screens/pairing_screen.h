#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  bool is_pairing_screen_active();
  void setup_pairing_properties();
  void handle_pairing_action();
  void teardown_pairing_properties();

#ifdef __cplusplus
}
#endif
