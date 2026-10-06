#ifndef OFFICIAL_HAND_LINK_H
#define OFFICIAL_HAND_LINK_H
#include "stm32f10x.h"
#include <stdint.h>
#include <stdbool.h>
/* HC3 only. All flight calculations remain in the supplied control.c. */
typedef struct {
 uint32_t owned,active,session,sequence,received,bad,last_ms,lock_ms;
 uint32_t reason,first_event,last_fault,rx_gap_ms,rx_max_gap_ms;
 uint32_t imu_ms,imu_ok,height_ms,height_frames,height_mm,height_ok;
 uint32_t stage,arm_latched,stage_ms,ground_mm,ground_set,assist;
 uint32_t recovery_since,recovery_ready,neutral_seen,collective;
 uint32_t radio_faults,radio_ready,replies,reply_failures;
 uint32_t control_us,last_control_us,max_control_us,overruns;
} HandLinkState;
extern volatile HandLinkState hand_link;
extern volatile uint32_t hand_milliseconds;
void hand_link_init(void);
void hand_link_poll(void);
bool hand_link_receive(const uint8_t raw[32]);
bool hand_link_owned(void);
void hand_link_tick(void);
bool hand_link_takeoff_target(float *target);
bool hand_link_landing_velocity(float *velocity);
void hand_link_imu(bool ok);
void hand_link_height(bool ok,uint16_t mm);
void hand_link_motor_observe(void);
uint32_t hand_time_us(void);
void hand_radio_init(void);
bool hand_radio_transmitting(void);
#endif
