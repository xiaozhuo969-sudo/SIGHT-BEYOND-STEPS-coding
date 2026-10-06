#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Local policy only; consumes accepted HC_TELEMETRY, never changes the wire protocol. */
enum { HAND_TELEMETRY_TIMEOUT_MS=600, HAND_CLEAR_DWELL_MS=500,
       HAND_OBSTACLE_CM=100, HAND_CLEAR_CM=120,
       HAND_TILT_CDEG=2000, HAND_LEVEL_CDEG=1500 };
typedef struct {
    bool hold, ready, tilt, sampled, clear_timing, level_timing;
    uint8_t tipped_samples;
    uint32_t sample_ms, clear_since, level_since;
    bool attitude_valid, roll_level_timing;
    int8_t roll_direction, roll_candidate;
    uint8_t roll_samples;
    uint32_t roll_level_since;
} hand_feedback_t;

static inline void hand_feedback_expire(hand_feedback_t *s, uint32_t now)
{
    if (!s->sampled || (uint32_t)(now-s->sample_ms)>=HAND_TELEMETRY_TIMEOUT_MS) {
        s->ready=s->clear_timing=s->level_timing=false;
        s->tipped_samples=0;
        s->roll_samples=0;s->roll_candidate=0;s->roll_level_timing=false;
        /* Missing data cannot clear a latched obstacle or create a new tilt edge. */
    }
}

/* Call once per NEW, session/sequence/CRC-validated aircraft telemetry frame. */
static inline void hand_feedback_sample(hand_feedback_t *s, uint32_t now,
    bool range_valid, uint16_t cm, bool attitude_valid, int16_t pitch, int16_t roll,
    bool obstacle_enabled)
{
    hand_feedback_expire(s,now);
    s->sampled=true;s->sample_ms=now;
    s->attitude_valid=attitude_valid;
    if (obstacle_enabled && range_valid && cm<=HAND_OBSTACLE_CM) s->hold=true;
    if (s->hold && range_valid && cm>HAND_CLEAR_CM) {
        if (!s->clear_timing) {s->clear_timing=true;s->clear_since=now;}
        s->ready=(uint32_t)(now-s->clear_since)>=HAND_CLEAR_DWELL_MS;
    } else s->ready=s->clear_timing=false;
    bool tipped=attitude_valid && (pitch>HAND_TILT_CDEG || pitch< -HAND_TILT_CDEG ||
                                  roll>HAND_TILT_CDEG || roll< -HAND_TILT_CDEG);
    bool level=attitude_valid && pitch<HAND_LEVEL_CDEG && pitch> -HAND_LEVEL_CDEG &&
                                roll<HAND_LEVEL_CDEG && roll> -HAND_LEVEL_CDEG;
    if (tipped) {
        if (s->tipped_samples<2) ++s->tipped_samples;
        if (s->tipped_samples==2) s->tilt=true;
    } else s->tipped_samples=0;
    if (s->tilt && level) {
        if (!s->level_timing) {s->level_timing=true;s->level_since=now;}
        if ((uint32_t)(now-s->level_since)>=HAND_CLEAR_DWELL_MS) s->tilt=false;
    } else s->level_timing=false;
    /* Aircraft roll is independent of the hand's gesture calibration. */
    int8_t direction=!attitude_valid ? 0 : roll>HAND_TILT_CDEG ? 1 :
        roll< -HAND_TILT_CDEG ? -1 : 0;
    if (direction) {
        if (s->roll_candidate!=direction) s->roll_samples=0;
        s->roll_candidate=direction;
        if (s->roll_samples<2) ++s->roll_samples;
        if (s->roll_samples==2) s->roll_direction=direction;
    } else {s->roll_samples=0;s->roll_candidate=0;}
    bool roll_level=attitude_valid && roll<HAND_LEVEL_CDEG && roll> -HAND_LEVEL_CDEG;
    if (s->roll_direction && roll_level) {
        if (!s->roll_level_timing) {s->roll_level_timing=true;s->roll_level_since=now;}
        if ((uint32_t)(now-s->roll_level_since)>=HAND_CLEAR_DWELL_MS) s->roll_direction=0;
    } else s->roll_level_timing=false;
}

/* Never display old or invalid attitude as a current aircraft direction. */
static inline int hand_feedback_roll_indicator(const hand_feedback_t *s, uint32_t now)
{
    return s->sampled && s->attitude_valid &&
        (uint32_t)(now-s->sample_ms)<HAND_TELEMETRY_TIMEOUT_MS ? s->roll_direction : 0;
}
