#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  bool is_stats_screen_active();
  void setup_stats_properties();
  void teardown_stats_properties();

#ifdef __cplusplus
}
#endif
