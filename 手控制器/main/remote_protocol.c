#include "remote_protocol.h"

bool remote_decode(const uint8_t *payload, size_t length, uint16_t channels[8])
{
    if (!payload || !channels || length != 32 || payload[0] != 0xAA ||
        payload[1] != 0xAF || payload[2] != 0x03 || payload[3] != 0x1B) return false;
    uint8_t sum = 0;
    for (size_t i = 0; i < 31; ++i) sum += payload[i];
    if (sum != payload[31]) return false;
    // Bytes 20..30 participate in the checksum but are not control channels.
    for (size_t i = 0; i < 8; ++i)
        channels[i] = ((uint16_t)payload[4 + i * 2] << 8) | payload[5 + i * 2];
    return true;
}

