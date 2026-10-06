#pragma once

#include "esp_err.h"

void vibration_start(void);
esp_err_t vibration_pulse(unsigned channel, unsigned strength, unsigned duration_ms);
