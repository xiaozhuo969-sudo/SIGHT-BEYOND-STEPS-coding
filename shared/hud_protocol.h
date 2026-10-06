#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HUD_VERSION 1
#define HUD_MAX_JSON 768
#define HUD_UPLOAD_MS 300
#define HUD_LINK_STALE_MS 1200
#define HUD_SPO2_STALE_MS 10000
#define HUD_VOLTAGE_STALE_MS 5000
#define HUD_MAX_AGE_MS 120000
#define HUD_NA INT32_MIN

/* Integer units on the wire; null is never confused with a genuine zero. */
typedef struct {
    uint32_t session, seq;
    int32_t spo2_x10, spo2_age_ms;
    int32_t voltage_mv, voltage_age_ms;
    int32_t pitch_cdeg, roll_cdeg, distance_cm, air_age_ms;
} hud_sample_t;

hud_sample_t hud_empty(void);
int32_t hud_age(uint64_t elapsed_ms);
void hud_advance_age(hud_sample_t *sample, uint64_t elapsed_ms);
bool hud_sequence_newer(uint32_t seq, uint32_t previous);
int hud_encode(const hud_sample_t *sample, char *buffer, size_t capacity);
bool hud_decode(const char *buffer, size_t length, hud_sample_t *sample);
