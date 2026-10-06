#include "gesture.h"
#include <math.h>

#define ENTER_DEG 20.0f
#define EXIT_DEG 12.0f
#define CENTER_DEG 10.0f
#define AXIS_MARGIN_DEG 10.0f
#define ACTIVE_MARGIN_DEG 5.0f
#define CENTER_MS 1000U

static void clear_motion(gesture_state_t *state)
{
    state->phase = 0;
    state->selected = GESTURE_UNKNOWN;
    state->pending = GESTURE_UNKNOWN;
    state->center_started = false;
}

void gesture_reset(gesture_state_t *state)
{
    state->zero_pitch = state->zero_roll = 0;
    state->calibrated = false;
    clear_motion(state);
}

void gesture_calibrate(gesture_state_t *state, float pitch, float roll)
{
    state->zero_pitch = pitch;
    state->zero_roll = roll;
    state->calibrated = true;
    clear_motion(state);
}

void gesture_set_direction(gesture_state_t *state, bool invert_pitch, bool invert_roll, bool swap_axes)
{
    state->invert_pitch = invert_pitch;
    state->invert_roll = invert_roll;
    state->swap_axes = swap_axes;
    clear_motion(state);
}

static float wrap_angle(float angle)
{
    return remainderf(angle, 360.0f);
}

static unsigned choose_direction(float pitch, float roll)
{
    const float p = fabsf(pitch), r = fabsf(roll);
    if (p >= ENTER_DEG && p >= r + AXIS_MARGIN_DEG)
        return pitch > 0 ? GESTURE_UP : GESTURE_DOWN;
    if (r >= ENTER_DEG && r >= p + AXIS_MARGIN_DEG)
        return roll > 0 ? GESTURE_RIGHT : GESTURE_LEFT;
    return GESTURE_UNKNOWN; // Ambiguous diagonals do not issue a new direction.
}

static bool still_active(unsigned signal, float pitch, float roll)
{
    float axis, cross;
    switch (signal) {
    case GESTURE_UP: axis = pitch; cross = fabsf(roll); break;
    case GESTURE_DOWN: axis = -pitch; cross = fabsf(roll); break;
    case GESTURE_RIGHT: axis = roll; cross = fabsf(pitch); break;
    case GESTURE_LEFT: axis = -roll; cross = fabsf(pitch); break;
    default: return false;
    }
    return axis > EXIT_DEG && axis >= cross + ACTIVE_MARGIN_DEG;
}

gesture_result_t gesture_update(gesture_state_t *state, float pitch, float roll,
                                bool valid, bool fist, uint32_t now_ms)
{
    gesture_result_t result = {.signal = fist ? GESTURE_FIST : GESTURE_UNKNOWN};
    result.valid = valid && isfinite(pitch) && isfinite(roll);
    if (!result.valid) {
        clear_motion(state);
        return result;
    }
    float p = wrap_angle(pitch - state->zero_pitch);
    float r = wrap_angle(roll - state->zero_roll);
    result.pitch = (state->swap_axes ? r : p) * (state->invert_pitch ? -1 : 1);
    result.roll = (state->swap_axes ? p : r) * (state->invert_roll ? -1 : 1);
    if (fist) {
        clear_motion(state);
        state->phase = 2;
        return result;
    }
    const unsigned candidate = choose_direction(result.pitch, result.roll);
    if (candidate != GESTURE_UNKNOWN ||
        still_active(state->selected, result.pitch, result.roll)) {
        // New clear directions switch immediately, without a center gate.
        const unsigned signal = candidate != GESTURE_UNKNOWN ? candidate : state->selected;
        clear_motion(state);
        state->phase = 2;
        state->selected = signal;
        result.signal = signal;
        return result;
    }
    state->selected = GESTURE_UNKNOWN;
    if (fabsf(result.pitch) > CENTER_DEG || fabsf(result.roll) > CENTER_DEG) {
        clear_motion(state);
        return result;
    }
    // No direction or fist, and both axes must remain centered for a full second.
    // While waiting output UNKNOWN (no command), never the previous movement.
    if (!state->center_started) {
        state->center_started = true;
        state->center_since = now_ms;
    }
    if ((uint32_t)(now_ms - state->center_since) >= CENTER_MS) {
        state->phase = 3;
        state->pending = GESTURE_UNKNOWN;
        result.signal = GESTURE_FLAT;
    } else {
        state->phase = 1;
        state->pending = GESTURE_FLAT;
    }
    return result;
}
