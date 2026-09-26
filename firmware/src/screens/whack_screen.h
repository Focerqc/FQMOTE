#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  void setup_whack_properties();
  void teardown_whack_properties();
  uint32_t whack_high_score();

#ifdef __cplusplus
}
#endif
