#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

/* History capacity, not a minimum sample count or a publication gate. */
#define PPG_HISTORY_SAMPLES 500
typedef struct {
    float red[PPG_HISTORY_SAMPLES], ir[PPG_HISTORY_SAMPLES];
    unsigned count;
} ppg_window_t;

/* Keep only a valid result from the current contact session. Holding does not
 * refresh its measurement timestamp or claim the current waveform is valid. */
typedef struct {
    float value;
    bool valid, held;
    int64_t updated_us;
} ppg_result_t;

static inline void ppg_result_update(ppg_result_t *r, bool finger, bool valid,
                                     float value, int64_t now_us)
{
    if (!finger) {
        *r = (ppg_result_t){0};
        return;
    }
    r->valid = valid && isfinite(value) && value >= 70 && value <= 100;
    if (r->valid) {
        r->value = value;
        r->updated_us = now_us;
    }
    r->held = !r->valid && r->updated_us > 0;
}

/* Contact/pressure steps dwarf the pulse in the recorded trace. Reset history
 * on >3% (at least 300 ADC counts) change within 10 samples at 100Hz.
 * This rejects a discontinuity, without delaying raw samples or smoothing SpO2. */
typedef struct {
    uint32_t red, ir;
    unsigned age;
    bool initialized;
} ppg_contact_t;

static inline bool ppg_contact_step(ppg_contact_t *c, uint32_t red, uint32_t ir)
{
    uint32_t dr=red>c->red?red-c->red:c->red-red;
    uint32_t di=ir>c->ir?ir-c->ir:c->ir-ir;
    uint32_t tr=c->red*3/100, ti=c->ir*3/100;
    if(tr<300)tr=300;
    if(ti<300)ti=300;
    bool step=c->initialized&&(dr>tr||di>ti);
    if(!c->initialized||step||++c->age>=10) {
        c->red=red;c->ir=ir;c->age=0;c->initialized=true;
    }
    return step;
}

static inline void ppg_window_append(ppg_window_t *w, const uint32_t *red,
                                     const uint32_t *ir, unsigned n)
{
    if (n >= PPG_HISTORY_SAMPLES) {
        red += n-PPG_HISTORY_SAMPLES; ir += n-PPG_HISTORY_SAMPLES;
        n = PPG_HISTORY_SAMPLES; w->count = 0;
    }
    if (w->count+n > PPG_HISTORY_SAMPLES) {
        unsigned drop = w->count+n-PPG_HISTORY_SAMPLES;
        w->count -= drop;
        memmove(w->red, w->red+drop, w->count*sizeof(float));
        memmove(w->ir, w->ir+drop, w->count*sizeof(float));
    }
    for (unsigned i=0; i<n; ++i) {
        w->red[w->count] = (float)red[i];
        w->ir[w->count++] = (float)ir[i];
    }
}
