#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include "../main/socket_address.h"
int main(void) {
    struct sockaddr_storage address = {0};
    uint32_t actual = 0, expected = inet_addr("192.168.4.1");
    struct sockaddr_in *v4 = (struct sockaddr_in *)&address;
    v4->sin_family = AF_INET; v4->sin_addr.s_addr = expected;
    assert(socket_address_ipv4(&address, &actual) && actual == expected);
    memset(&address, 0, sizeof(address));
    struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)&address;
    v6->sin6_family = AF_INET6;
    assert(inet_pton(AF_INET6, "::ffff:192.168.4.1", &v6->sin6_addr) == 1);
    assert(socket_address_ipv4(&address, &actual) && actual == expected);
    assert(inet_pton(AF_INET6, "::ffff:192.168.0.108", &v6->sin6_addr) == 1);
    assert(socket_address_ipv4(&address, &actual) && actual != expected);
    assert(inet_pton(AF_INET6, "::1", &v6->sin6_addr) == 1);
    assert(!socket_address_ipv4(&address, &actual));
    return 0;
}
