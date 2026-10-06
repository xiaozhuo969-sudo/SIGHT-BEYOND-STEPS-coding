/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <sys/param.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_check.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "dns_server.h"

#define DNS_PORT (53)
#define DNS_MAX_LEN (512)

#define OPCODE_MASK (0x7800)
#define QR_FLAG (1 << 7)
#define QD_TYPE_A (0x0001)
#define ANS_TTL_SEC (300)

static const char *TAG = "example_dns_redirect_server";

// DNS Header Packet
typedef struct __attribute__((__packed__))
{
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

// DNS Question Packet
typedef struct {
    uint16_t type;
    uint16_t class;
} dns_question_t;

// DNS Answer Packet
typedef struct __attribute__((__packed__))
{
    uint16_t ptr_offset;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t addr_len;
    uint32_t ip_addr;
} dns_answer_t;

// DNS server handle
struct dns_server_handle {
    atomic_bool started;
    SemaphoreHandle_t finished;
    TaskHandle_t task;
    int num_of_entries;
    dns_entry_pair_t entry[];
};

// Bounded single-question parser. Reject compressed/truncated QNAMEs instead
// of allowing an unauthenticated UDP packet to read outside the receive buffer.
static int parse_dns_request(char *req, size_t len, char *reply, size_t cap, dns_server_handle_t h)
{
    if (len < 12 || len > cap || (uint8_t)req[2] & 0xf8 ||
        (uint8_t)req[4] != 0 || (uint8_t)req[5] != 1) return -1;
    size_t pos = 12, used = 0;
    char name[256];
    while (pos < len && req[pos]) {
        unsigned n = (uint8_t)req[pos++];
        if (n > 63 || n > len - pos || used + n + 1 >= sizeof(name)) return -1;
        if (used) name[used++] = '.';
        memcpy(name + used, req + pos, n); used += n; pos += n;
    }
    if (pos >= len || len - ++pos < 4) return -1;
    name[used] = 0;
    unsigned type = ((uint8_t)req[pos] << 8) | (uint8_t)req[pos+1];
    unsigned cls = ((uint8_t)req[pos+2] << 8) | (uint8_t)req[pos+3];
    pos += 4;
    memcpy(reply, req, pos);
    reply[2] = (req[2] & 1) | 0x80; reply[3] = 0;
    memset(reply + 6, 0, 6);
    if (type != 1 || cls != 1) return pos;
    esp_ip4_addr_t ip = {0};
    for (int i = 0; i < h->num_of_entries; ++i) {
        if (strcmp(h->entry[i].name, "*") && strcmp(h->entry[i].name, name)) continue;
        if (h->entry[i].if_key) {
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey(h->entry[i].if_key);
            esp_netif_ip_info_t info;
            if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK) ip = info.ip;
        } else ip = h->entry[i].ip;
        break;
    }
    if (!ip.addr || cap - pos < sizeof(dns_answer_t)) return pos;
    dns_answer_t answer = {.ptr_offset=htons(0xc00c), .type=htons(1),
        .class=htons(1), .ttl=htonl(30), .addr_len=htons(4), .ip_addr=ip.addr};
    memcpy(reply + pos, &answer, sizeof(answer));
    reply[7] = 1;
    return pos + sizeof(answer);
}

/*
    Sets up a socket and listen for DNS queries,
    replies to all type A queries with the IP of the softAP
*/
void dns_server_task(void *pvParameters)
{
    char rx_buffer[DNS_MAX_LEN];
    char addr_str[128];
    int addr_family;
    int ip_protocol;
    dns_server_handle_t handle = pvParameters;

    while (handle->started) {

        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(DNS_PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        struct timeval timeout = {.tv_sec=0, .tv_usec=250000};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
            close(sock); break;
        }
        ESP_LOGI(TAG, "Socket bound, port %d", DNS_PORT);

        while (handle->started) {
            
            struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            // Error occurred during receiving
            if (len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;
            }
            // Data received
            else {
                // Get the sender's ip address as string
                if (source_addr.sin6_family == PF_INET) {
                    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                } else if (source_addr.sin6_family == PF_INET6) {
                    inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
                }

                // Null-terminate whatever we received and treat like a string...
                rx_buffer[len] = 0;

                char reply[DNS_MAX_LEN];
                int reply_len = parse_dns_request(rx_buffer, len, reply, DNS_MAX_LEN, handle);

                ESP_LOGI(TAG, "Received %d bytes from %s | DNS reply with len: %d", len, addr_str, reply_len);
                if (reply_len <= 0) {
                    ESP_LOGE(TAG, "Failed to prepare a DNS reply");
                } else {
                    int err = sendto(sock, reply, reply_len, 0, (struct sockaddr *)&source_addr, socklen);
                    if (err < 0) {
                        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                        break;
                    }
                }
            }
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket");
            shutdown(sock, 0);
            close(sock);
        }
    }
    xSemaphoreGive(handle->finished);
    vTaskDelete(NULL);
}

dns_server_handle_t start_dns_server(dns_server_config_t *config)
{
    dns_server_handle_t handle = calloc(1, sizeof(struct dns_server_handle) + config->num_of_entries * sizeof(dns_entry_pair_t));
    ESP_RETURN_ON_FALSE(handle, NULL, TAG, "Failed to allocate dns server handle");

    handle->started = true;
    handle->num_of_entries = config->num_of_entries;
    memcpy(handle->entry, config->item, config->num_of_entries * sizeof(dns_entry_pair_t));

    handle->finished = xSemaphoreCreateBinary();
    if (!handle->finished) { free(handle); return NULL; }
    if (xTaskCreate(dns_server_task, "dns_server", 4096, handle, 5, &handle->task) != pdPASS) {
        vSemaphoreDelete(handle->finished); free(handle); return NULL;
    }
    return handle;
}

void stop_dns_server(dns_server_handle_t handle)
{
    if (handle) {
        handle->started = false;
        xSemaphoreTake(handle->finished, portMAX_DELAY);
        vSemaphoreDelete(handle->finished);
        free(handle);
    }
}
