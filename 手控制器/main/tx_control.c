#include "tx_control.h"
#include <string.h>
#include "../../shared/hand_protocol.h"

static bool airborne(const tx_control_t *s)
{
    return s->v3 && s->phase>=TX_ALT_TAKEOFF && s->phase<=TX_ALT_RESUME;
}
void tx_guard_refresh(tx_control_t *s, uint32_t now)
{
    if (!airborne(s)) s->guard=(hand_feedback_t){0};
    else {
        hand_feedback_expire(&s->guard,now);
        if (s->phase==TX_ALT_LAND)
            s->guard.hold=s->guard.ready=s->guard.clear_timing=false;
    }
}
static void phase(tx_control_t *s, unsigned next, uint32_t now)
{
    s->phase = next; s->since = now; s->sent = 0;
    tx_guard_refresh(s,now);
}
bool tx_start_v3(tx_control_t *s, uint32_t now, uint32_t session)
{
    if (s->phase != TX_OFF && s->phase != TX_FAULT) return false;
    const bool estop=s->estop_active;const uint32_t stopped=s->estop_ms;
    *s = (tx_control_t){.phase=TX_READY,.since=now,.heartbeat=now,.v2=true,.v3=true,
        .session=session?session:1,.air_height_mm=UINT16_MAX,.air_relative_mm=INT16_MIN,
        .air_pitch_cdeg=INT16_MIN,.air_roll_cdeg=INT16_MIN,.air_distance_cm=UINT16_MAX};
    s->estop_active=estop;s->estop_ms=stopped;
    if(estop) s->reason=TX_REASON_STOP;
    return true;
}
bool tx_arm(tx_control_t *s, uint32_t now, bool flat_calibrated)
{
    if (!tx_can_arm(s, now) || !flat_calibrated) return false;
    s->estop_active=false;s->reason=TX_REASON_NONE;
    phase(s, TX_ARM_REQUEST, now);
    return true;
}
void tx_stop(tx_control_t *s, uint32_t now, unsigned reason)
{
    if (s->phase == TX_OFF || s->phase == TX_FAULT || s->phase == TX_STOPPING) return;
    s->reason = reason;
    phase(s, TX_STOPPING, now);
}
uint32_t tx_estop_remaining(const tx_control_t *s, uint32_t now)
{
    uint32_t elapsed=now-s->estop_ms;
    return s->estop_active && elapsed<10000 ? 10000-elapsed : 0;
}
void tx_estop(tx_control_t *s, uint32_t now)
{
    s->estop_active=true;s->estop_new_session=true;s->estop_ms=now;s->reason=TX_REASON_STOP;
    /* Keep an established session and sequence; radio recovery may start a new one. */
    if(s->phase!=TX_OFF && s->phase!=TX_FAULT) phase(s,TX_READY,now);
    s->feedback=false;s->guard=(hand_feedback_t){0};
}
void tx_estop_refresh_session(tx_control_t *s, uint32_t now, uint32_t session)
{
    /* First DISARM the current flight session. Only a fresh locked reply permits
     * a new session to clear the aircraft's per-session fault latch. */
    if(!s->estop_active || !s->estop_new_session || s->phase!=TX_READY || !s->feedback ||
       (uint32_t)(now-s->feedback_ms)>=300 || (s->air_flags&1) || s->air_command!=HC_DISARM) return;
    if(!session || session==s->session) {session=s->session+1;if(!session)session=1;}
    s->phase=TX_OFF;
    tx_start_v3(s,now,session); // Preserves emergency cooldown, clears feedback only.
}
void tx_fault(tx_control_t *s)
{
    s->phase = TX_FAULT; s->reason = TX_REASON_RADIO; s->guard=(hand_feedback_t){0};
}
void tx_sent(tx_control_t *s) { if (s->sent < 10000) ++s->sent; }

static void link_health(tx_control_t *s, uint32_t now)
{
    if (!s->v2) return;
    uint32_t age = now - (s->feedback ? s->feedback_ms : s->since);
    unsigned warning = age >= 600 ? 2 : age >= 300 ? 1 : 0;
    if (warning) {
        if (warning > s->link_warning) s->link_warning = warning;
        s->recovering = false;
    }
}

bool tx_step(tx_control_t *s, uint32_t now, bool pose_valid, bool flat,
             const uint16_t preview[8], uint16_t channels[8])
{
    if (s->phase == TX_OFF || s->phase == TX_FAULT) return false;
    /* Emergency lock communication does not depend on hand pose or USB heartbeat.
     * This branch can only encode DISARM, and never clears the re-arm gate. */
    if(s->estop_active) {
        if(s->phase!=TX_READY) phase(s,TX_READY,now);
        link_health(s,now);
        channels[0]=channels[1]=channels[3]=1500;channels[2]=1000;channels[4]=2000;
        channels[5]=channels[6]=channels[7]=1000;
        return true;
    }
    if ((uint32_t)(now - s->heartbeat) > 750) tx_stop(s, now, TX_REASON_HEARTBEAT);
    if (!pose_valid) tx_stop(s, now, TX_REASON_IMU);
    if (s->v2) {
        if (s->phase == TX_ARM_REQUEST && (uint32_t)(now-s->since)>3000)
            tx_stop(s,now,TX_REASON_ARM_TIMEOUT);
        /* Downlink loss says nothing about the aircraft's control reception.
         * Keep sending live control; the aircraft owns its uplink watchdog. */
        link_health(s, now);
        if (s->phase == TX_ALT_RESUME && (uint32_t)(now-s->since)>3000)
            phase(s, TX_ALT_MANUAL, now);
    }
    tx_guard_refresh(s,now);
    (void)flat;
    if (s->phase==TX_STOPPING && s->sent>=15 && (uint32_t)(now-s->since)>=500) {
        phase(s,TX_OFF,now);return false;
    }
    channels[0]=channels[1]=channels[3]=1500;
    channels[2]=1000;channels[4]=2000;
    channels[5]=channels[6]=channels[7]=1000;
    if(s->v3 && s->phase>=TX_ALT_IDLE && s->phase<=TX_ALT_RESUME) {
        channels[2]=1500;channels[4]=2000;
        if(s->phase==TX_ALT_LIVE||s->phase==TX_ALT_MANUAL) memcpy(channels,preview,8*sizeof(*channels));
        if(s->phase==TX_ALT_LIVE||s->phase==TX_ALT_MANUAL) {
            if(s->guard.hold) {
                channels[0]=channels[1]=channels[3]=1500;
                /* In manual height-loss recovery, retain pilot vertical control. */
                if(s->phase==TX_ALT_LIVE) channels[2]=1500;
            } else if((!s->telemetry || !s->air_distance_valid ||
                       (uint32_t)(now-s->telemetry_ms)>=HAND_TELEMETRY_TIMEOUT_MS) && channels[1]>1500)
                channels[1]=1500;
        }
    }
    return true;
}

bool tx_clear_obstacle(tx_control_t *s, uint32_t now)
{
    tx_guard_refresh(s,now);
    if(!s->v3 || (s->phase!=TX_ALT_LIVE && s->phase!=TX_ALT_MANUAL) ||
       !s->feedback || (uint32_t)(now-s->feedback_ms)>=300 || s->link_warning ||
       !(s->air_flags&1) || !s->guard.hold || !s->guard.ready) return false;
    s->guard.hold=s->guard.ready=s->guard.clear_timing=false;
    return true;
}
bool tx_can_takeoff(const tx_control_t *s, uint32_t now, bool flat_calibrated)
{
    return s->v3 && s->phase==TX_ALT_IDLE && flat_calibrated && s->feedback &&
        !s->link_warning && (uint32_t)(now-s->feedback_ms)<300 && (s->air_flags&1) &&
        s->air_stage==HC_STAGE_IDLE;
}
bool tx_takeoff(tx_control_t *s, uint32_t now, bool flat_calibrated)
{
    if(!tx_can_takeoff(s,now,flat_calibrated)) return false;
    phase(s,TX_ALT_TAKEOFF,now);return true;
}
bool tx_land(tx_control_t *s, uint32_t now)
{
    if(!s->v3 || (s->phase!=TX_ALT_TAKEOFF&&s->phase!=TX_ALT_LIVE)) return false;
    phase(s,TX_ALT_LAND,now);return true;
}
bool tx_can_resume(const tx_control_t *s, uint32_t now, bool flat_calibrated)
{
    return s->v3 && s->phase==TX_ALT_MANUAL && flat_calibrated &&
        s->feedback && !s->link_warning && (uint32_t)(now-s->feedback_ms)<300 &&
        (s->air_flags&1) && s->air_stage==HC_STAGE_MANUAL && s->resume_ready;
}
bool tx_resume(tx_control_t *s, uint32_t now, bool flat_calibrated)
{
    if (!tx_can_resume(s,now,flat_calibrated)) return false;
    phase(s,TX_ALT_RESUME,now);return true;
}
bool tx_can_arm(const tx_control_t *s, uint32_t now)
{
    return !tx_estop_remaining(s,now) && s->v3 && s->phase==TX_READY && s->feedback && !s->link_warning &&
        (uint32_t)(now-s->feedback_ms)<300 && (uint32_t)(now-s->since)>=500 &&
        !(s->air_flags&1) && s->air_command==HC_DISARM;
}
void tx_encode(tx_control_t *s, const uint16_t channels[8], uint8_t packet[32])
{
    hc_packet_t p={.session=s->session,.sequence=++s->sequence,.assist=3,.version=3};
    p.command=s->phase==TX_READY||s->phase==TX_STOPPING?HC_DISARM:
              s->phase==TX_ARM_REQUEST?HC_ARM:HC_CONTROL;
    if(s->v3 && s->phase==TX_ALT_TAKEOFF) p.command=HC_TAKEOFF;
    else if(s->v3 && s->phase==TX_ALT_LAND) p.command=HC_LAND;
    else if(s->v3 && s->phase==TX_ALT_RESUME) p.command=HC_RESUME;
    memcpy(p.channels,channels,sizeof p.channels);
    hc_encode(&p,packet);
}
void tx_feedback(tx_control_t *s, const uint8_t packet[32], uint32_t now)
{
    hc_packet_t p;
    if (!s->v2 || s->phase==TX_OFF || s->phase==TX_FAULT) return;
    if (!hc_decode(packet,&p) || p.version!=(s->v3?3:2) ||
        (p.command!=HC_STATUS&&p.command!=HC_DETAIL&&p.command!=HC_DIAGNOSTICS&&p.command!=HC_TELEMETRY)) {
        ++s->rejected_schema;return;
    }
    if (p.session!=s->session) { ++s->rejected_session;return; }
    if ((uint16_t)(s->sequence-p.sequence)>16 ||
        (s->feedback&&!hc_newer(p.sequence,s->feedback_sequence))) {
        ++s->rejected_sequence;return;
    }
    link_health(s,now);
    // Only a fresh, matching aircraft STATUS counts as a received reply.
    s->air_gap_ms=s->feedback?(uint32_t)(now-s->feedback_ms):0;
    if(s->air_gap_ms>s->air_max_gap_ms) s->air_max_gap_ms=s->air_gap_ms;
    ++s->air_frames;
    if (s->link_warning) {
        if (!s->recovering) { s->recovering=true;s->recovery_ms=now; }
        else if ((uint32_t)(now-s->recovery_ms)>=500) s->link_warning=0;
    }
    s->air_rx_count=p.channels[4];s->air_thr=p.channels[2];
    if(p.command==HC_STATUS) s->air_bad=p.channels[7];
    else if (p.command==HC_DETAIL) {
        s->detail=true;s->detail_ms=now;s->air_height_mm=p.channels[5];
        s->air_collective=p.channels[6];s->air_radio_faults=p.channels[7];
    } else if (p.command==HC_DIAGNOSTICS) {
        s->diagnostics=true;s->diagnostics_ms=now;
        s->air_rx_gap_ms=p.channels[5];s->air_rx_max_gap_ms=p.channels[6];s->air_first_event=p.channels[7];
    } else if (p.command==HC_TELEMETRY) {
        s->air_telem_gap_ms=s->telemetry?(uint32_t)(now-s->telemetry_ms):0;
        if(s->air_telem_gap_ms>s->air_telem_max_gap_ms) s->air_telem_max_gap_ms=s->air_telem_gap_ms;
        s->telemetry=true;s->telemetry_ms=now;++s->air_telem_frames;
        s->air_attitude_valid=(p.assist&HC_ATTITUDE_VALID)!=0;
        s->air_distance_valid=(p.assist&HC_DISTANCE_VALID)!=0;
        s->air_pitch_cdeg=s->air_attitude_valid?(int16_t)p.channels[5]:INT16_MIN;
        s->air_roll_cdeg=s->air_attitude_valid?(int16_t)p.channels[6]:INT16_MIN;
        s->air_distance_cm=s->air_distance_valid?p.channels[7]:UINT16_MAX;
    }
    s->air_seq=p.sequence;
    s->feedback=true;s->feedback_ms=now;s->feedback_sequence=p.sequence;
    s->air_flags=p.channels[0];s->air_reason=p.channels[1];s->air_command=p.channels[3];
    if(s->v3) {
        s->air_stage=p.state&0x0f;s->air_height_valid=(p.state&HC_STATE_HEIGHT_FRESH)!=0;
        s->air_ground_ready=(p.state&HC_STATE_GROUND_READY)!=0;s->air_relative_mm=(int16_t)p.height;
        s->resume_ready=(p.state&HC_STATE_RESUME_READY)!=0;
        if(s->phase==TX_ARM_REQUEST && s->air_command==HC_ARM && (s->air_flags&1) &&
           s->air_stage==HC_STAGE_IDLE && s->air_reason==HC_OK) phase(s,TX_ALT_IDLE,now);
        else if(s->phase>=TX_ALT_IDLE && s->phase<=TX_ALT_RESUME) {
            if(!(s->air_flags&1)) tx_stop(s,now,TX_REASON_AIR_LOCK);
            else if(s->air_stage==HC_STAGE_HEIGHT_LOST) phase(s,TX_ALT_LOST,now);
            else if(s->air_stage==HC_STAGE_MANUAL && (s->phase!=TX_ALT_RESUME||
                    (s->air_command==HC_RESUME&&s->air_reason!=HC_OK))) phase(s,TX_ALT_MANUAL,now);
            else if(s->air_stage==HC_STAGE_LAND &&
                    (s->phase==TX_ALT_TAKEOFF||s->phase==TX_ALT_LIVE)) phase(s,TX_ALT_LAND,now);
            else if((s->phase==TX_ALT_TAKEOFF||s->phase==TX_ALT_RESUME||s->phase==TX_ALT_MANUAL) &&
                    s->air_stage==HC_STAGE_HOLD && (s->air_flags&2))
                phase(s,TX_ALT_LIVE,now);
            else if(s->phase==TX_ALT_TAKEOFF && s->air_stage==HC_STAGE_IDLE &&
                    s->air_command==HC_TAKEOFF && s->air_reason!=HC_OK) phase(s,TX_ALT_IDLE,now);
        }
        tx_guard_refresh(s,now);
        if(p.command==HC_TELEMETRY && airborne(s))
            hand_feedback_sample(&s->guard,now,s->air_distance_valid,s->air_distance_cm,
                s->air_attitude_valid,s->air_pitch_cdeg,s->air_roll_cdeg,s->phase!=TX_ALT_LAND);
        return;
    }

}

bool tx_can_zero(const tx_control_t *s)
{
    return s->phase == TX_OFF || s->phase == TX_READY || s->phase == TX_FAULT;
}
