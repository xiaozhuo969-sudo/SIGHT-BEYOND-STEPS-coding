#pragma once
#include <stdbool.h>

void led_demo_start(void);
bool flight_stop_pressed(void);
// Bit 0: GPIO10 level; bit 1: GPIO11 level, before debounce or latching.
unsigned flight_keys_raw_levels(void);
