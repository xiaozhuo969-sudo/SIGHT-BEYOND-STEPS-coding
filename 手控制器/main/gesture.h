#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    GESTURE_UNKNOWN = 0, GESTURE_FLAT = 1, GESTURE_LEFT = 2, GESTURE_RIGHT = 3,
    GESTURE_UP = 4, GESTURE_DOWN = 5, GESTURE_FIST = 6,
};
typedef struct {
    float zero_pitch, zero_roll;
    bool calibrated, invert_pitch, invert_roll, swap_axes;
    // 0: no action/invalid, 1: confirming neutral, 2: direction/fist, 3: neutral.
    unsigned phase, selected, pending;
    uint32_t center_since;
    bool center_started;
} gesture_state_t;
// Default mounting: previous left <-> up, previous right <-> down.
// Logical pitch = -relative roll; logical roll = -relative pitch.
#define GESTURE_INITIALIZER { .invert_pitch = true, .invert_roll = true, .swap_axes = true }
typedef struct {
    float pitch, roll;
    unsigned signal; // Exactly one enum value, never a bit mask.
    bool valid;
} gesture_result_t;

void gesture_reset(gesture_state_t *state);
void gesture_calibrate(gesture_state_t *state, float pitch, float roll);
void gesture_set_direction(gesture_state_t *state, bool invert_pitch, bool invert_roll, bool swap_axes);
gesture_result_t gesture_update(gesture_state_t *state, float pitch, float roll,
                                bool valid, bool fist, uint32_t now_ms);
