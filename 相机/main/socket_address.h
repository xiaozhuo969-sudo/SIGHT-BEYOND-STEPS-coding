#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
// Include the platform's sockets header before this file.
static inline bool socket_address_ipv4(const struct sockaddr_storage *address, uint32_t *ipv4)
{
    if (address->ss_family == AF_INET) {
        *ipv4 = ((const struct sockaddr_in *)address)->sin_addr.s_addr;
        return true;
    }
#if defined(AF_INET6)
    if (address->ss_family == AF_INET6) {
        const uint8_t *bytes = ((const struct sockaddr_in6 *)address)->sin6_addr.s6_addr;
        static const uint8_t prefix[12] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff};
        if (memcmp(bytes, prefix, sizeof(prefix)) == 0) {
            memcpy(ipv4, bytes + 12, sizeof(*ipv4));
            return true;
        }
    }
#endif
    return false;
}
