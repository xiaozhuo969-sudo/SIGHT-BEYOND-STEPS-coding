#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Original STM32 remote: fixed 32-byte AA AF 03 1B frame, 8 big-endian channels.
bool remote_decode(const uint8_t *payload, size_t length, uint16_t channels[8]);

