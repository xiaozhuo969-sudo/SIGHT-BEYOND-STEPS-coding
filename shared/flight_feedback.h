#ifndef FLIGHT_FEEDBACK_H
#define FLIGHT_FEEDBACK_H
#include <stdbool.h>
#include <stdint.h>

/* Flight-side policy, evaluated every 3 ms. No PID or motor outputs here. */
typedef struct {
    bool hold, clear_timing, ready, tilt, tilt_timing, level_timing;
    uint32_t clear_since, tilt_since, level_since;
} flight_feedback_t;

static inline void flight_feedback_update(flight_feedback_t *s, uint32_t now,
    bool flying, bool range_valid, uint16_t cm, uint32_t range_ms,
    bool attitude_valid, float pitch, float roll)
{
    if (!flying) { *s=(flight_feedback_t){0}; return; }
    if (range_valid && cm<=100) s->hold=true;
    if (s->hold && range_valid && cm>120) {
        if (!s->clear_timing) { s->clear_timing=true; s->clear_since=now; }
        /* Require new sensor evidence at the end of the 500 ms dwell. */
        s->ready=(uint32_t)(now-s->clear_since)>=500 &&
                 (int32_t)(range_ms-s->clear_since)>=500;
    } else { s->clear_timing=false; s->ready=false; }
    bool tipped=attitude_valid && (pitch>30 || pitch< -30 || roll>30 || roll< -30);
    bool level=attitude_valid && pitch<25 && pitch> -25 && roll<25 && roll> -25;
    if (tipped) {
        if (!s->tilt_timing) { s->tilt_timing=true; s->tilt_since=now; }
        if ((uint32_t)(now-s->tilt_since)>=200) s->tilt=true;
    } else s->tilt_timing=false;
    if (s->tilt && level) {
        if (!s->level_timing) { s->level_timing=true; s->level_since=now; }
        if ((uint32_t)(now-s->level_since)>=500) s->tilt=false;
    } else s->level_timing=false;
}

static inline bool flight_feedback_clear(flight_feedback_t *s)
{
    if (!s->hold || !s->ready) return false;
    s->hold=s->ready=s->clear_timing=false;
    return true;
}
#endif
