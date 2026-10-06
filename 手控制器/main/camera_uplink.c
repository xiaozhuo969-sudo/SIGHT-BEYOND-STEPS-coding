#include "camera_uplink.h"
#include "input_voltage.h"
#include "telemetry.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ws.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"

static const char *TAG="camera_uplink";
static EventGroupHandle_t network_events;
#define HAS_IP BIT0
static portMUX_TYPE status_mux=portMUX_INITIALIZER_UNLOCKED;
static int64_t last_ack_us;
static void upload_status(bool ok) {
    portENTER_CRITICAL(&status_mux);
    last_ack_us=ok?esp_timer_get_time():0;
    portEXIT_CRITICAL(&status_mux);
}
bool camera_uplink_healthy(void) {
    portENTER_CRITICAL(&status_mux);
    int64_t stamp=last_ack_us;
    portEXIT_CRITICAL(&status_mux);
    return stamp && esp_timer_get_time()-stamp<1000000;
}

static void wifi_event(void *arg,esp_event_base_t base,int32_t event,void *data)
{
    (void)arg; (void)data;
    if (base==IP_EVENT && event==IP_EVENT_STA_GOT_IP) xEventGroupSetBits(network_events,HAS_IP);
    else if (base==WIFI_EVENT && event==WIFI_EVENT_STA_DISCONNECTED) xEventGroupClearBits(network_events,HAS_IP);
}
static esp_err_t start_wifi(void)
{
    esp_err_t err=nvs_flash_init(); // Never erase NVS as a recovery shortcut.
    if (err!=ESP_OK) return err;
    err=esp_netif_init(); if (err!=ESP_OK) return err;
    err=esp_event_loop_create_default(); if (err!=ESP_OK && err!=ESP_ERR_INVALID_STATE) return err;
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    err=esp_wifi_init(&init); if (err!=ESP_OK) return err;
    err=esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL); if (err!=ESP_OK) return err;
    err=esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL); if (err!=ESP_OK) return err;
    wifi_config_t config={0};
    strlcpy((char *)config.sta.ssid,CONFIG_HAND_CAMERA_SSID,sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password,CONFIG_HAND_CAMERA_PASSWORD,sizeof(config.sta.password));
    config.sta.threshold.authmode=strlen(CONFIG_HAND_CAMERA_PASSWORD) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable=true;
    err=esp_wifi_set_storage(WIFI_STORAGE_RAM); if (err!=ESP_OK) return err;
    err=esp_wifi_set_mode(WIFI_MODE_STA); if (err!=ESP_OK) return err;
    err=esp_wifi_set_config(WIFI_IF_STA,&config); if (err!=ESP_OK) return err;
    err=esp_wifi_start(); if (err!=ESP_OK) return err;
    return esp_wifi_set_ps(WIFI_PS_NONE);
}
static bool receive_ack(esp_transport_handle_t ws,uint32_t session,uint32_t seq)
{
    char expected[80],received[80];
    int wanted=snprintf(expected,sizeof(expected),"{\"ack\":%"PRIu32",\"session\":%"PRIu32"}",seq,session);
    int used=0; int64_t deadline=esp_timer_get_time()+500000;
    while (used<wanted && esp_timer_get_time()<deadline) {
        int remaining=(int)((deadline-esp_timer_get_time())/1000);
        if (remaining<=0) return false;
        int n=esp_transport_read(ws,received+used,(int)sizeof(received)-used,remaining);
        if (n<0) return false;
        if (!n) continue;
        if (esp_transport_ws_get_read_opcode(ws)!=WS_TRANSPORT_OPCODES_TEXT ||
            !esp_transport_ws_get_fin_flag(ws) || esp_transport_ws_get_read_payload_len(ws)!=wanted) return false;
        used+=n;
    }
    return used==wanted && !memcmp(received,expected,wanted);
}
static void uplink_task(void *arg)
{
    (void)arg;
    network_events=xEventGroupCreate();
    if (!network_events) { vTaskDelete(NULL); return; }
    esp_err_t err=start_wifi();
    if (err!=ESP_OK) {
        ESP_LOGE(TAG,"Wi-Fi uplink unavailable: %s",esp_err_to_name(err));
        vTaskDelete(NULL); return;
    }
    esp_transport_handle_t tcp=esp_transport_tcp_init();
    esp_transport_handle_t ws=tcp ? esp_transport_ws_init(tcp) : NULL;
    if (!ws) { if (tcp) esp_transport_destroy(tcp); vTaskDelete(NULL); return; }
    esp_transport_ws_set_path(ws,"/ws/upload");
    uint32_t session=esp_random(); if (!session) session=1;
    uint32_t seq=0,sent=0,failures=0;
    int64_t next_wifi=0,last_log=0;
    bool connected=false;
    ESP_LOGI(TAG,"Camera=%s at %s; upload every %d ms",CONFIG_HAND_CAMERA_SSID,CONFIG_HAND_CAMERA_HOST,HUD_UPLOAD_MS);
    for (;;) {
        if (!(xEventGroupGetBits(network_events)&HAS_IP)) {
            upload_status(false);
            if (connected) { esp_transport_close(ws); connected=false; }
            int64_t now=esp_timer_get_time();
            if (now>=next_wifi) {
                esp_wifi_connect(); next_wifi=now+5000000;
                ESP_LOGI(TAG,"Waiting for camera hotspot %s",CONFIG_HAND_CAMERA_SSID);
            }
            vTaskDelay(pdMS_TO_TICKS(300)); continue;
        }
        if (!connected) {
            esp_transport_close(ws);
            /* A timed-out partial frame remains in the transport parser.
               Each TCP connection needs a fresh WebSocket parser. */
            esp_transport_destroy(ws);
            ws=esp_transport_ws_init(tcp);
            if (!ws) { esp_transport_destroy(tcp); vTaskDelete(NULL); return; }
            esp_transport_ws_set_path(ws,"/ws/upload");
            if (esp_transport_connect(ws,CONFIG_HAND_CAMERA_HOST,80,1000)<0) {
                vTaskDelay(pdMS_TO_TICKS(1000)); continue;
            }
            int no_delay=1;
            setsockopt(esp_transport_get_socket(ws),IPPROTO_TCP,TCP_NODELAY,&no_delay,sizeof(no_delay));
            connected=true; ESP_LOGI(TAG,"WebSocket connected");
        }
        const int64_t began=esp_timer_get_time();
        hud_sample_t sample; telemetry_hud_snapshot(&sample); input_voltage_snapshot(&sample);
        sample.session=session; sample.seq=++seq;
        char body[HUD_MAX_JSON]; int len=hud_encode(&sample,body,sizeof(body));
        /* One acknowledged snapshot in flight. On congestion close the socket;
           no offline queue or catch-up burst can block the NRF24 task. */
        if (len<0 || esp_transport_ws_send_raw(ws,WS_TRANSPORT_OPCODES_TEXT|WS_TRANSPORT_OPCODES_FIN,body,len,100)!=len ||
            !receive_ack(ws,session,seq)) {
            ++failures; upload_status(false); esp_transport_close(ws); connected=false;
        } else { ++sent; upload_status(true); }
        if (began-last_log>=5000000) {
            ESP_LOGI(TAG,"sent=%"PRIu32" failures=%"PRIu32" input_mV=%"PRId32" aircraft_age_ms=%"PRId32,
                sent,failures,sample.voltage_mv==HUD_NA ? -1 : sample.voltage_mv,
                sample.air_age_ms==HUD_NA ? -1 : sample.air_age_ms);
            last_log=began;
        }
        int64_t remaining=HUD_UPLOAD_MS*1000-(esp_timer_get_time()-began);
        vTaskDelay(pdMS_TO_TICKS(remaining>1000 ? (uint32_t)(remaining/1000) : 1));
    }
}
void camera_uplink_start(void)
{
    if (xTaskCreate(uplink_task,"camera_uplink",6144,NULL,2,NULL)!=pdPASS)
        ESP_LOGE(TAG,"No memory for camera uplink task");
}
