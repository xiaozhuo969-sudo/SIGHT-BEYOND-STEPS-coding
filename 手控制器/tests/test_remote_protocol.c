#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "remote_protocol.h"
int main(void)
{
    // Live STM32 RAM snapshot, including its nonzero extra bytes.
    uint8_t packet[32] = {0xaa,0xaf,3,0x1b,5,0xe7,5,0xd4,3,0xe9,5,0xe4,
        3,0xe8,3,0xe8,3,0xe8,3,0xe8,0xaf,0xaa,0x1b,3,0xe7,5,0,0,0,0,0,0x20};
    uint16_t channels[8] = {0};
    assert(remote_decode(packet, 32, channels));
    assert(channels[0] == 1511 && channels[1] == 1492 && channels[2] == 1001);
    assert(channels[3] == 1508 && channels[7] == 1000);
    uint16_t previous[8]; memcpy(previous, channels, sizeof(previous));
    for (int i = 0; i < 32; ++i) {
        packet[i] ^= 1;
        assert(!remote_decode(packet, 32, channels));
        assert(memcmp(previous, channels, sizeof(previous)) == 0);
        packet[i] ^= 1;
    }
    assert(!remote_decode(packet, 31, channels));
    assert(!remote_decode(packet, 33, channels));
    assert(!remote_decode(NULL, 32, channels));
    // Even a checksummed wrong command must not be accepted.
    packet[2]++;packet[31]++;
    assert(!remote_decode(packet, 32, channels));
    puts("PASS: real STM32 frame, endian order, checksum, bounds, wrong command, no stale overwrite");
}
