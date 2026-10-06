#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "gyro_calibration.h"

static const float level[3] = {0, 0, 1000};
static const float bias[3] = {0.25f, -0.5f, 0.75f};

static void stationary_window(gyro_calibration_t *s, const float accel[3], uint32_t start)
{
    for (unsigned i=0; i<=200; ++i) {
        // Realistic, zero-mean noise around known offsets.
        const float n = i==200 ? 0 : (i%2 ? -0.0625f : 0.0625f);
        const float gyro[3] = {bias[0]+n, bias[1]-n, bias[2]+n};
        assert(gyro_calibration_update(s, accel, gyro, start+i*10) == (i==200));
        assert(gyro_calibration_progress(s) == i/2);
    }
    assert(s->state == GYRO_CAL_READY && s->count == 201);
    for (unsigned i=0; i<3; ++i) {
        assert(fabsf(s->bias[i]-bias[i]) < 0.00001f);
        assert(fabsf(bias[i]-s->bias[i]) < 0.00001f); // Corrected stationary rate.
        assert(fabsf(s->accel_mean[i]-accel[i]) < 0.02f);
    }
}

int main(void)
{
    gyro_calibration_t s = {0};
    assert(gyro_calibration_progress(&s) == 0);
    stationary_window(&s, level, 0);
    // Ready is permanent for this boot, even after motion / sensor errors.
    gyro_calibration_restart(&s, GYRO_CAL_ERROR);
    const float moving[3] = {90, -80, 100};
    assert(gyro_calibration_update(&s, level, moving, 4000));
    assert(s.count == 201 && s.bias[1] == bias[1]);

    // An arbitrary stationary mounting angle and uint32 timestamp wrap are valid.
    s = (gyro_calibration_t){0};
    const float tilted[3] = {300, 400, 866.0254f};
    stationary_window(&s, tilted, UINT32_MAX-1000);

    // Elapsed time alone cannot satisfy the minimum sample count.
    s = (gyro_calibration_t){0};
    for (unsigned i=0; i<200; ++i)
        assert(gyro_calibration_update(&s, level, bias, i*20) == (i==199));
    assert(s.count == 200);

    // Repeated samples do not advance; interrupted windows must start over.
    s = (gyro_calibration_t){0};
    for (unsigned i=0; i<100; ++i) {
        assert(!gyro_calibration_update(&s, level, bias, i*10));
        assert(!gyro_calibration_update(&s, level, bias, i*10));
    }
    assert(s.count == 100);
    assert(!gyro_calibration_update(&s, level, bias, 1100));
    assert(s.count == 101 && s.first_ms == 0); // Scheduling jitter is not motion.
    assert(!gyro_calibration_update(&s, level, bias, 1351));
    assert(s.count == 1 && s.first_ms == 1351);
    gyro_calibration_restart(&s, GYRO_CAL_ERROR);
    assert(s.count == 0 && gyro_calibration_progress(&s) == 0);
    stationary_window(&s, level, 2000);

    // Each axis participates in motion detection, and failures clear all sums.
    for (unsigned axis=0; axis<3; ++axis) {
        s = (gyro_calibration_t){0};
        assert(!gyro_calibration_update(&s, level, bias, 0));
        float g[3] = {bias[0], bias[1], bias[2]};
        g[axis] += 1.1f;
        assert(!gyro_calibration_update(&s, level, g, 10));
        assert(s.state == GYRO_CAL_MOVING && s.count == 0);
        stationary_window(&s, level, 20);

        s = (gyro_calibration_t){0};
        assert(!gyro_calibration_update(&s, level, bias, 0));
        float a[3] = {0, 0, 1000}; a[axis] += 31;
        assert(!gyro_calibration_update(&s, a, bias, 10));
        assert(s.state == GYRO_CAL_MOVING && s.count == 0);
        stationary_window(&s, level, 20);

        s = (gyro_calibration_t){0}; g[axis] = -6;
        assert(!gyro_calibration_update(&s, level, g, 0));
        assert(s.state == GYRO_CAL_MOVING && s.count == 0);
        g[axis] = NAN;
        assert(!gyro_calibration_update(&s, level, g, 10));
        assert(s.state == GYRO_CAL_ERROR);
        a[axis] = INFINITY;
        assert(!gyro_calibration_update(&s, a, bias, 20));
        assert(s.state == GYRO_CAL_ERROR);
    }
    for (unsigned i=0; i<2; ++i) {
        s = (gyro_calibration_t){0};
        const float a[3] = {0, 0, i ? 1200 : 0};
        assert(!gyro_calibration_update(&s, a, bias, 0));
        assert(s.state == GYRO_CAL_MOVING && s.count == 0);
    }
    puts("gyro_calibration tests passed");
    return 0;
}
