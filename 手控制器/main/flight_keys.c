#include "flight_keys.h"

void flight_keys_stop(flight_keys_t *k, tx_control_t *tx, uint32_t now)
{
    k->pending=false; k->launch_requested=false; k->start_armed=false;
    k->local_owner=false; k->auto_connect=true; k->stop_latched=true; k->reason=KEY_ESTOP;
    tx_estop(tx,now);
    k->status=KEY_STOPPED;
}

void flight_keys_init(flight_keys_t *k, tx_control_t *tx, bool start_down, bool stop_down, uint32_t now)
{
    *k=(flight_keys_t){.initialized=true,.down={start_down,stop_down},.start_released=!start_down,
        .auto_connect=tx->phase==TX_OFF};
    if(k->auto_connect) k->status=KEY_AUTO_CONNECT;
    if(stop_down) flight_keys_stop(k,tx,now);
}

void flight_keys_autoconnect(flight_keys_t *k, tx_control_t *tx, uint32_t now, uint32_t session,
                           bool radio_ready, bool imu_fresh)
{
    if(!k->initialized) return;
    if(tx->estop_active) {
        if(radio_ready && (tx->phase==TX_OFF || tx->phase==TX_FAULT)) tx_start_v3(tx,now,session);
        tx_estop_refresh_session(tx,now,session);
        tx->heartbeat=now;
        if(k->pending) return;
        k->local_owner=false;k->status=KEY_STOPPED;
        k->reason=tx_estop_remaining(tx,now)?KEY_COOLDOWN:k->down[1]?KEY_ESTOP:
            !radio_ready?KEY_NO_RADIO:!tx_can_arm(tx,now)?KEY_NO_LOCK_REPLY:KEY_OK;
        return;
    }
    if(!k->auto_connect || k->stop_latched || k->down[1]) return;
    k->status=KEY_AUTO_CONNECT;
    k->reason=!radio_ready ? KEY_NO_RADIO : !imu_fresh ? KEY_NO_IMU : KEY_OK;
    if(!radio_ready || !imu_fresh) {
        k->local_owner=false;
        tx_stop(tx,now,!imu_fresh ? TX_REASON_IMU : TX_REASON_RADIO);
        return;
    }
    if(tx->phase==TX_OFF || tx->phase==TX_FAULT) {
        if(!tx_start_v3(tx,now,session)) return;
    }
    if(tx->phase!=TX_READY) { k->local_owner=false; return; }
    k->local_owner=true;
    tx->heartbeat=now;
    if(tx_can_arm(tx,now)) k->status=KEY_LINK_READY;
    // No calibration, ARM, or TAKEOFF here, even with fresh replies and a flat hand.
}

void flight_keys_edge(flight_keys_t *k, tx_control_t *tx, unsigned key, bool down, uint32_t now)
{
    if(!k->initialized || key>1 || k->down[key]==down) return;
    k->down[key]=down;
    if(down) ++k->presses[key];
    if(key==1) {
        if(down) flight_keys_stop(k,tx,now);
        return;
    }
    if(down) {
        k->start_armed=k->start_released && !k->down[1] && !k->pending &&
            (!tx->estop_active || tx_can_arm(tx,now));
    } else {
        if(k->start_armed && !k->down[1]) k->launch_requested=true;
        k->start_armed=false;
        k->start_released=true;
    }
}

bool flight_keys_begin(flight_keys_t *k, tx_control_t *tx, uint32_t now, uint32_t session,
                       bool radio_ready, bool imu_fresh, bool palm_down)
{
    if(!k->launch_requested) return false;
    k->launch_requested=false;
    unsigned reason=k->down[1] ? KEY_ESTOP : !radio_ready ? KEY_NO_RADIO :
                    tx_estop_remaining(tx,now) ? KEY_COOLDOWN :
                    (tx->estop_active && !tx_can_arm(tx,now)) ? KEY_NO_LOCK_REPLY :
                    !imu_fresh ? KEY_NO_IMU : palm_down ? KEY_PALM :
                    (tx->phase!=TX_OFF && tx->phase!=TX_FAULT && tx->phase!=TX_READY &&
                     tx->phase!=TX_ALT_IDLE) ? KEY_BUSY : KEY_OK;
    if(reason!=KEY_OK) {
        k->reason=reason;
        if(!k->local_owner) k->status=KEY_REJECTED;
        return false;
    }
    if(tx->phase==TX_OFF || tx->phase==TX_FAULT) {
        if(!tx_start_v3(tx,now,session)) return false;
    }
    k->stop_latched=false; k->auto_connect=false; k->pending=true; k->local_owner=true;
    k->started=now; k->reason=KEY_OK; k->status=KEY_CONNECTING;
    return true;
}

static void cancel(flight_keys_t *k, tx_control_t *tx, uint32_t now, unsigned reason)
{
    k->pending=false; k->local_owner=false; k->launch_requested=false;
    k->start_armed=false; k->status=KEY_REJECTED; k->reason=reason;
    tx_stop(tx,now,reason==KEY_NO_IMU ? TX_REASON_IMU : TX_REASON_STOP);
}

void flight_keys_tick(flight_keys_t *k, tx_control_t *tx, uint32_t now,
                      bool radio_ready, bool imu_fresh, bool flat, bool centering, bool palm_down)
{
    if(k->down[1]) return; // The press edge already stopped; holding never restarts the timer.
    if(k->status==KEY_STOPPING && (tx->phase==TX_OFF || tx->phase==TX_FAULT)) k->status=KEY_STOPPED;
    if(k->auto_connect) return; // Boot link heartbeat/status comes from autoconnect.
    if(!k->local_owner) return;
    if(tx->phase==TX_OFF || tx->phase==TX_FAULT || tx->phase==TX_STOPPING) {
        cancel(k,tx,now,tx->phase==TX_FAULT ? KEY_NO_RADIO : KEY_BUSY); return;
    }
    if(!radio_ready || !imu_fresh) {
        cancel(k,tx,now,!radio_ready ? KEY_NO_RADIO : KEY_NO_IMU); return;
    }
    // Refreshed by the independent 50ms control task, never by USB logging.
    // If this task stops, tx_step's original 750ms watchdog still expires.
    tx->heartbeat=now;
    if(k->pending) {
        if(palm_down || (!flat && !centering)) {
            cancel(k,tx,now,palm_down ? KEY_PALM : KEY_NOT_CENTERED); return;
        }
        if((uint32_t)(now-k->started)>6000) { cancel(k,tx,now,KEY_TIMEOUT); return; }
        if(tx->phase==TX_READY && tx_arm(tx,now,flat)) k->status=KEY_ARMING;
        else if(tx->phase==TX_ALT_IDLE && tx_takeoff(tx,now,flat)) {
            k->pending=false; k->status=KEY_TAKEOFF;
        }
    } else if(tx->phase==TX_ALT_TAKEOFF) k->status=KEY_TAKEOFF;
    else k->status=KEY_ACTIVE;
}

void flight_keys_host_start(flight_keys_t *k)
{
    k->pending=false; k->local_owner=false; k->launch_requested=false;
    k->start_armed=false; k->auto_connect=false; k->stop_latched=false; k->status=KEY_IDLE; k->reason=KEY_OK;
}
