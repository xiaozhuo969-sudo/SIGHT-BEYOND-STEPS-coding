#pragma once

#include "esp_err.h"
#include <stdbool.h>
void vibration_alerts(bool obstacle, bool tilt);

void vibration_start(void);
esp_err_t vibration_pulse(unsigned channel, unsigned strength, unsigned duration_ms);
