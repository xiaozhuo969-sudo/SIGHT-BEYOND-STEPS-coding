#ifndef HAPTIC_POLICY_H
#define HAPTIC_POLICY_H
#include <stdbool.h>
#include <stdint.h>

/* One event per rising edge; tilt preempts and pauses an obstacle pulse. */
typedef struct {
    bool obstacle_seen, tilt_seen;
    uint32_t remaining[2], last_ms;
    unsigned active; /* 0=off, 1=MOT0, 2=MOT1 */
} haptic_policy_t;
static inline unsigned haptic_update(haptic_policy_t *s, uint32_t now,
                                     bool obstacle, bool tilt)
{
    uint32_t dt=now-s->last_ms; s->last_ms=now;
    if (s->active) {
        uint32_t *left=&s->remaining[s->active-1];
        *left=dt>=*left ? 0 : *left-dt;
    }
    /* A second range edge must not refill an active or paused 3-second pulse. */
    if (obstacle && !s->obstacle_seen && !s->remaining[0]) s->remaining[0]=3000;
    if (tilt && !s->tilt_seen) s->remaining[1]=3000;
    s->obstacle_seen=obstacle; s->tilt_seen=tilt;
    s->active=s->remaining[1] ? 2 : s->remaining[0] ? 1 : 0;
    return s->active;
}
#endif
