#pragma once
#include "settings_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

  int settings_store_input_state(const InputPinSettings *pins, const CalibrationSettings *calibration);
  int settings_load_input_state(InputPinSettings *pins, CalibrationSettings *calibration);
  void settings_reset_calibration(CalibrationSettings *calibration, bool reset_x, bool reset_y);

#ifdef __cplusplus
}
#endif
