#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { GYRO_CAL_WAITING, GYRO_CAL_SAMPLING, GYRO_CAL_MOVING, GYRO_CAL_READY, GYRO_CAL_ERROR };
typedef struct {
    unsigned state, count;
    uint32_t first_ms, last_ms;
    float gyro_sum[3], accel_sum[3], gyro_min[3], gyro_max[3], accel_min[3], accel_max[3];
    float bias[3], accel_mean[3];
} gyro_calibration_t;

// Startup-only stationary mean. Inputs: acceleration in mg, angular rate in deg/s.
void gyro_calibration_restart(gyro_calibration_t *s, unsigned state);
bool gyro_calibration_update(gyro_calibration_t *s, const float accel[3], const float gyro[3], uint32_t now_ms);
unsigned gyro_calibration_progress(const gyro_calibration_t *s);
