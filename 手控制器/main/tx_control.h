#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "hand_feedback.h"

/* Keep published phase IDs stable; obsolete stick-unlock phases are removed. */
enum { TX_OFF=0, TX_READY=1, TX_STOPPING=9, TX_FAULT=10, TX_ARM_REQUEST=11,
       TX_ALT_IDLE=12, TX_ALT_TAKEOFF=13, TX_ALT_LIVE=14, TX_ALT_LAND=15,
       TX_ALT_LOST=16, TX_ALT_MANUAL=17, TX_ALT_RESUME=18 };
enum { TX_REASON_NONE, TX_REASON_STOP, TX_REASON_HEARTBEAT, TX_REASON_IMU, TX_REASON_RADIO,
       TX_REASON_FEEDBACK, TX_REASON_ARM_TIMEOUT, TX_REASON_AIR_LOCK };
typedef struct {
    unsigned phase, reason, sent;
    bool estop_active;
    bool estop_new_session;
    uint32_t estop_ms;
    hand_feedback_t guard;
    uint32_t since, heartbeat;
    uint16_t aux;
    bool v2, feedback, v3, detail;
    uint32_t session, feedback_ms;
    uint16_t sequence, feedback_sequence, air_flags, air_reason, air_command;
    uint16_t air_rx_count, air_bad, air_thr, air_seq;
    uint32_t air_frames, air_gap_ms, air_max_gap_ms;
    uint8_t air_stage;
    bool air_height_valid, air_ground_ready;
    int16_t air_relative_mm;
    uint16_t air_height_mm, air_collective, air_radio_faults;
    uint32_t detail_ms;
    unsigned link_warning;
    bool recovering, resume_ready, diagnostics;
    uint32_t recovery_ms, diagnostics_ms;
    uint32_t rejected_schema, rejected_session, rejected_sequence;
    uint16_t air_rx_gap_ms, air_rx_max_gap_ms, air_first_event;
    bool telemetry, air_attitude_valid, air_distance_valid;
    int16_t air_pitch_cdeg, air_roll_cdeg;
    uint16_t air_distance_cm;
    uint32_t telemetry_ms, air_telem_frames, air_telem_gap_ms, air_telem_max_gap_ms;
} tx_control_t;
bool tx_clear_obstacle(tx_control_t *s, uint32_t now);
void tx_guard_refresh(tx_control_t *s, uint32_t now);
bool tx_obstacle_alarm(const tx_control_t *s);
unsigned tx_obstacle_indicator(const tx_control_t *s, uint32_t now);
bool tx_arm(tx_control_t *s, uint32_t now, bool flat_calibrated);
void tx_stop(tx_control_t *s, uint32_t now, unsigned reason);
void tx_estop(tx_control_t *s, uint32_t now);
void tx_estop_refresh_session(tx_control_t *s, uint32_t now, uint32_t session);
uint32_t tx_estop_remaining(const tx_control_t *s, uint32_t now);
void tx_fault(tx_control_t *s);
void tx_sent(tx_control_t *s);
bool tx_step(tx_control_t *s, uint32_t now, bool pose_valid, bool flat,
             const uint16_t preview[8], uint16_t channels[8]);

bool tx_can_zero(const tx_control_t *s);
bool tx_start_v3(tx_control_t *s, uint32_t now, uint32_t session);
bool tx_can_takeoff(const tx_control_t *s, uint32_t now, bool flat_calibrated);
bool tx_takeoff(tx_control_t *s, uint32_t now, bool flat_calibrated);
bool tx_land(tx_control_t *s, uint32_t now);
bool tx_can_resume(const tx_control_t *s, uint32_t now, bool flat_calibrated);
bool tx_resume(tx_control_t *s, uint32_t now, bool flat_calibrated);
bool tx_can_arm(const tx_control_t *s, uint32_t now);
void tx_encode(tx_control_t *s, const uint16_t channels[8], uint8_t packet[32]);
void tx_feedback(tx_control_t *s, const uint8_t packet[32], uint32_t now);
