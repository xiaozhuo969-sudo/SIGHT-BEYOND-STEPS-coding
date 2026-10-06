#pragma once
#include <stdbool.h>
#include <stdint.h>

/* HC3: confirmed flat moves forward; palm press/transitions are neutral. */
void control_output_altitude(bool yaw_axis, unsigned signal, bool valid, uint16_t channels[8]);
