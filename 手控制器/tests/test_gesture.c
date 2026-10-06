#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "gesture.h"

static unsigned step(gesture_state_t *s, float p, float r, uint32_t t)
{
    return gesture_update(s, p, r, true, false, t).signal;
}
int main(void)
{
    const struct { float p, r; unsigned code; } dirs[] = {
        {0, -30, GESTURE_LEFT}, {0, 30, GESTURE_RIGHT},
        {30, 0, GESTURE_UP}, {-30, 0, GESTURE_DOWN},
    };
    for (unsigned i = 0; i < 4; ++i) {
        for (unsigned j = 0; j < 4; ++j) {
            gesture_state_t s = {0};
            assert(step(&s, dirs[i].p, dirs[i].r, 0) == dirs[i].code);
            // All direction pairs switch on the first sample, no center gate.
            assert(step(&s, dirs[j].p, dirs[j].r, 100) == dirs[j].code);
            assert(gesture_update(&s, 0, 30, true, true, 200).signal == GESTURE_FIST);
            assert(step(&s, dirs[i].p, dirs[i].r, 300) == dirs[i].code);
            // Crossing center must never briefly output neutral or hold a direction.
            assert(step(&s, 0, 0, 400) == GESTURE_UNKNOWN);
            assert(step(&s, 0, 0, 1399) == GESTURE_UNKNOWN);
            assert(step(&s, dirs[j].p, dirs[j].r, 1400) == dirs[j].code);
            assert(step(&s, 0, 0, 1500) == GESTURE_UNKNOWN);
            assert(step(&s, 0, 0, 2499) == GESTURE_UNKNOWN);
            assert(step(&s, 0, 0, 2500) == GESTURE_FLAT);
            assert(step(&s, 0, 0, 10000) == GESTURE_FLAT);
            assert(step(&s, dirs[i].p, dirs[i].r, 10100) == dirs[i].code);
        }
    }
    gesture_state_t s = {0};
    assert(step(&s, 0, 0, 0) == GESTURE_UNKNOWN);
    assert(step(&s, 0, 0, 999) == GESTURE_UNKNOWN);
    assert(step(&s, 0, 0, 1000) == GESTURE_FLAT);
    // Leaving the center region, even without a direction, restarts the timer.
    assert(step(&s, 11, 0, 1100) == GESTURE_UNKNOWN);
    assert(step(&s, 10, -10, 1200) == GESTURE_UNKNOWN);
    assert(step(&s, 30, 30, 2000) == GESTURE_UNKNOWN);
    assert(step(&s, 30, 30, 5000) == GESTURE_UNKNOWN);
    assert(step(&s, 0, 0, 5100) == GESTURE_UNKNOWN);
    assert(step(&s, 0, 0, 6100) == GESTURE_FLAT);
    // Fist interrupts neutral confirmation, release cannot leave a stale fist.
    assert(gesture_update(&s, 0, 0, true, true, 6200).signal == GESTURE_FIST);
    assert(step(&s, 0, 0, 6300) == GESTURE_UNKNOWN);
    assert(step(&s, 0, 0, 7299) == GESTURE_UNKNOWN);
    assert(step(&s, 0, 0, 7300) == GESTURE_FLAT);
    assert(step(&s, 20, 0, 7400) == GESTURE_UP);
    assert(step(&s, 13, 0, 7500) == GESTURE_UP);
    assert(step(&s, 12, 0, 7600) == GESTURE_UNKNOWN);
    gesture_calibrate(&s, 10, -7);
    assert(step(&s, 40, -7, 7700) == GESTURE_UP);
    gesture_set_direction(&s, true, true, false);
    assert(step(&s, 40, -7, 7800) == GESTURE_DOWN);
    gesture_set_direction(&s, false, false, true);
    assert(step(&s, 40, -7, 7900) == GESTURE_RIGHT);
    gesture_reset(&s);
    assert(!s.calibrated && s.swap_axes && s.phase == 0);
    gesture_set_direction(&s, false, false, false);
    gesture_calibrate(&s, 0, 179);
    assert(fabsf(gesture_update(&s, 0, -179, true, false, 8000).roll - 2) < .01f);
    assert(gesture_update(&s, 0, 0, false, true, 8100).signal == GESTURE_FIST);
    assert(gesture_update(&s, NAN, 0, true, false, 8200).signal == GESTURE_UNKNOWN);
    s = (gesture_state_t){0};
    assert(step(&s, 0, 0, UINT32_MAX - 500) == GESTURE_UNKNOWN);
    assert(step(&s, 0, 0, 498) == GESTURE_UNKNOWN);
    assert(step(&s, 0, 0, 499) == GESTURE_FLAT);
    puts("PASS: immediate exclusive directions, neutral-only delay, interrupted center, fist release, mounting, stale, timer wrap");
}
