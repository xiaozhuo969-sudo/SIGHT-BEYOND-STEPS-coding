#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "tx_control.h"

enum { KEY_IDLE, KEY_CONNECTING, KEY_ARMING, KEY_TAKEOFF, KEY_ACTIVE,
       KEY_STOPPING, KEY_STOPPED, KEY_REJECTED, KEY_AUTO_CONNECT, KEY_LINK_READY, KEY_LANDING };
enum { KEY_OK, KEY_NO_RADIO, KEY_NO_IMU, KEY_PALM, KEY_BUSY, KEY_NOT_CENTERED,
       KEY_TIMEOUT, KEY_ESTOP, KEY_COOLDOWN, KEY_NO_LOCK_REPLY };
typedef struct {
    bool initialized, down[2], start_released, start_armed, launch_requested;
    bool pending, local_owner, stop_latched, auto_connect;
    bool palm_down,combo_latched,landing_active;
    unsigned status, reason, presses[2];
    uint32_t started;
} flight_keys_t;

void flight_keys_init(flight_keys_t *k, tx_control_t *tx, bool start_down, bool stop_down, uint32_t now);
void flight_keys_palm(flight_keys_t *k, tx_control_t *tx, bool down, uint32_t now);
void flight_keys_edge(flight_keys_t *k, tx_control_t *tx, unsigned key, bool down, uint32_t now);
/* Boot and post-estop communication send DISARM only; launch is always explicit. */
void flight_keys_autoconnect(flight_keys_t *k, tx_control_t *tx, uint32_t now, uint32_t session,
                           bool radio_ready, bool imu_fresh);
bool flight_keys_begin(flight_keys_t *k, tx_control_t *tx, uint32_t now, uint32_t session,
                       bool radio_ready, bool imu_fresh, bool palm_down);
void flight_keys_tick(flight_keys_t *k, tx_control_t *tx, uint32_t now,
                      bool radio_ready, bool imu_fresh, bool flat, bool centering, bool palm_down);
void flight_keys_stop(flight_keys_t *k, tx_control_t *tx, uint32_t now);
/* An explicit host START3/ARM selects the existing USB heartbeat mode. */
void flight_keys_host_start(flight_keys_t *k);
