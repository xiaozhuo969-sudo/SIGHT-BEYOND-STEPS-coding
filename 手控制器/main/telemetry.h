#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "../../shared/hud_protocol.h"

void telemetry_indicator_snapshot(bool *air, bool *obstacle, unsigned *gesture, int *test_pin);
void telemetry_start(void);
void telemetry_set_attitude(float pitch, float roll);
void telemetry_set_imu_calibration(unsigned status, unsigned progress, const float bias[3]);
void telemetry_set_spo2(bool finger, bool valid, float spo2, int samples, bool new_result);
void telemetry_hud_snapshot(hud_sample_t *sample);
void telemetry_set_ppg(bool ready, bool reading_ok, uint32_t red, uint32_t ir, unsigned samples);
void telemetry_set_ppg_quality(float correlation, float quality, uint32_t contact_resets);
void telemetry_set_button(bool pressed);
void telemetry_flight_keys_init(bool start_down, bool stop_down);
void telemetry_flight_key(unsigned key, bool pressed);
void telemetry_set_led(bool enabled, unsigned pattern);
void telemetry_set_motor(unsigned channel, unsigned strength);

void telemetry_set_radio_ready(bool ready);
void telemetry_radio_packet(const uint16_t channels[8]); // NULL = rejected packet.

bool telemetry_tx_frame(uint8_t packet[32]);
void telemetry_tx_result(bool success);

unsigned telemetry_tx_interval_ms(void);
void telemetry_air_packet(const uint8_t packet[32]);
void telemetry_tx_rx_time(uint32_t microseconds);
