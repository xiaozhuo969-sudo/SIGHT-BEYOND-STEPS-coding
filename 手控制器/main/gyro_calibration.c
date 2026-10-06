#include "gyro_calibration.h"
#include <math.h>

#define REQUIRED_SAMPLES 200U
#define STILL_TIME_MS 3000U
#define GYRO_WINDOW_SPAN_DPS 5.0f

void gyro_calibration_restart(gyro_calibration_t *s, unsigned state)
{
    // Never learn a new bias during normal operation / flight.
    if(s->state==GYRO_CAL_READY) return;
    *s=(gyro_calibration_t){.state=state};
}

bool gyro_calibration_update(gyro_calibration_t *s, const float a[3], const float g[3], uint32_t now)
{
    if(s->state==GYRO_CAL_READY) return true;
    float norm2=0;
    for(unsigned i=0;i<3;++i) {
        if(!isfinite(a[i]) || !isfinite(g[i])) {
            gyro_calibration_restart(s,GYRO_CAL_ERROR);return false;
        }
        norm2+=a[i]*a[i];
        if(fabsf(g[i])>5.0f) {
            gyro_calibration_restart(s,GYRO_CAL_MOVING);return false;
        }
    }
    if(norm2<900.0f*900.0f || norm2>1100.0f*1100.0f) {
        gyro_calibration_restart(s,GYRO_CAL_MOVING);return false;
    }
    if(s->count && now==s->last_ms) return false;
    // Wi-Fi / USB task scheduling can delay reads by >50 ms on this board.
    // Keep the sample-count requirement; only discard a genuinely stale window.
    if(s->count && (uint32_t)(now-s->last_ms)>250)
        gyro_calibration_restart(s,GYRO_CAL_WAITING);
    for(unsigned i=0;i<3;++i) {
        if(!s->count) {
            s->gyro_min[i]=s->gyro_max[i]=g[i];
            s->accel_min[i]=s->accel_max[i]=a[i];
        } else {
            s->gyro_min[i]=fminf(s->gyro_min[i],g[i]);
            s->gyro_max[i]=fmaxf(s->gyro_max[i],g[i]);
            s->accel_min[i]=fminf(s->accel_min[i],a[i]);
            s->accel_max[i]=fmaxf(s->accel_max[i],a[i]);
            if(s->gyro_max[i]-s->gyro_min[i]>GYRO_WINDOW_SPAN_DPS || s->accel_max[i]-s->accel_min[i]>30.0f) {
                gyro_calibration_restart(s,GYRO_CAL_MOVING);return false;
            }
        }
        s->gyro_sum[i]+=g[i];s->accel_sum[i]+=a[i];
    }
    if(!s->count) s->first_ms=now;
    ++s->count;s->last_ms=now;s->state=GYRO_CAL_SAMPLING;
    if(s->count<REQUIRED_SAMPLES || (uint32_t)(now-s->first_ms)<STILL_TIME_MS) return false;
    for(unsigned i=0;i<3;++i) {
        s->bias[i]=s->gyro_sum[i]/s->count;
        s->accel_mean[i]=s->accel_sum[i]/s->count;
    }
    s->state=GYRO_CAL_READY;
    return true;
}

unsigned gyro_calibration_progress(const gyro_calibration_t *s)
{
    if(s->state==GYRO_CAL_READY) return 100;
    unsigned points=s->count*100/REQUIRED_SAMPLES;
    unsigned elapsed=s->count?(uint32_t)(s->last_ms-s->first_ms)*100/STILL_TIME_MS:0;
    unsigned progress=points<elapsed?points:elapsed;
    return progress<100?progress:99;
}
