/* Optional on-device check of the REAL HTTPD server and live hand-controller
 * uploads. Uses TCP loopback; never sends controller/aircraft commands or
 * substitutes telemetry. Rebuild with CAMERA_HUD_SELFTEST=OFF for delivery. */
#include "../../shared/hud_protocol.h"
#include <inttypes.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ws.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

typedef struct { esp_transport_handle_t tcp,ws; } test_viewer_t;
typedef struct { bool connected; hud_sample_t data; } snapshot_t;
static const char *TAG="hud_selftest";

static bool open_viewer(test_viewer_t *v)
{
    v->tcp=esp_transport_tcp_init();
    v->ws=v->tcp ? esp_transport_ws_init(v->tcp) : NULL;
    if (!v->ws) return false;
    esp_transport_ws_set_path(v->ws,"/ws/view");
    if (esp_transport_connect(v->ws,"127.0.0.1",80,1500)<0) return false;
    int enabled=1;
    setsockopt(esp_transport_get_socket(v->ws),IPPROTO_TCP,TCP_NODELAY,&enabled,sizeof(enabled));
    return true;
}
static void close_viewer(test_viewer_t *v)
{
    if (v->ws) { esp_transport_close(v->ws); esp_transport_destroy(v->ws); }
    if (v->tcp) esp_transport_destroy(v->tcp);
    memset(v,0,sizeof(*v));
}
static bool read_snapshot(test_viewer_t *v,snapshot_t *out,int timeout_ms)
{
    char body[HUD_MAX_JSON+160]; int used=0,wanted=-1;
    int64_t deadline=esp_timer_get_time()+(int64_t)timeout_ms*1000;
    do {
        int wait=(int)((deadline-esp_timer_get_time())/1000);
        if (wait<=0 || used>=(int)sizeof(body)-1) return false;
        int n=esp_transport_read(v->ws,body+used,sizeof(body)-1-used,wait);
        if (n<0) return false;
        if (!n) continue;
        wanted=esp_transport_ws_get_read_payload_len(v->ws);
        if (esp_transport_ws_get_read_opcode(v->ws)!=WS_TRANSPORT_OPCODES_TEXT ||
            !esp_transport_ws_get_fin_flag(v->ws) || wanted<=0 || wanted>=(int)sizeof(body)) return false;
        used+=n;
    } while (used!=wanted);
    body[used]=0;
    cJSON *root=cJSON_ParseWithLength(body,used+1);
    const cJSON *connected=cJSON_GetObjectItemCaseSensitive(root,"connected");
    const cJSON *data=cJSON_GetObjectItemCaseSensitive(root,"data");
    const cJSON *age=cJSON_GetObjectItemCaseSensitive(root,"uplink_age_ms");
    char sample[HUD_MAX_JSON];
    bool ok=cJSON_IsBool(connected) && cJSON_IsNumber(age) &&
        (!cJSON_IsTrue(connected) || (age->valuedouble>=0 && age->valuedouble<HUD_LINK_STALE_MS)) &&
        cJSON_IsObject(data) && cJSON_PrintPreallocated((cJSON *)data,sample,sizeof(sample),false) &&
        hud_decode(sample,strlen(sample),&out->data);
    out->connected=cJSON_IsTrue(connected);
    cJSON_Delete(root); return ok;
}
static bool read_live(test_viewer_t *v,unsigned count,hud_sample_t *last)
{
    int64_t deadline=esp_timer_get_time()+25000000;
    bool have_previous=false; unsigned received=0;
    while (received<count) {
        int wait=(int)((deadline-esp_timer_get_time())/1000);
        snapshot_t snapshot;
        if (wait<=0 || !read_snapshot(v,&snapshot,wait)) return false;
        if (!snapshot.connected) continue;
        if (have_previous && (snapshot.data.session!=last->session ||
            !hud_sequence_newer(snapshot.data.seq,last->seq))) return false;
        *last=snapshot.data; have_previous=true; ++received;
    }
    return last->voltage_mv!=HUD_NA && last->voltage_mv>0 &&
        last->voltage_age_ms>=0 && last->voltage_age_ms<HUD_VOLTAGE_STALE_MS;
}
static void check_task(void *unused)
{
    (void)unused;
    test_viewer_t first={0},second={0}; snapshot_t initial;
    hud_sample_t last=hud_empty(); const char *failure=NULL;
    vTaskDelay(pdMS_TO_TICKS(2000));
    if (!open_viewer(&first)) { failure="initial HTTP/WebSocket handshake"; goto done; }
    /* The browser sends no greeting: the server must register a passive viewer
       and immediately send its first snapshot at handshake completion. */
    if (!read_snapshot(&first,&initial,1500)) { failure="no valid initial snapshot after handshake"; goto done; }
    ESP_LOGI(TAG,"Initial snapshot received; connected=%d",initial.connected);
    if (!read_live(&first,12,&last)) { failure="12 increasing live controller snapshots"; goto done; }
    ESP_LOGI(TAG,"Live viewer: session=%"PRIu32" seq=%"PRIu32" input_mV=%"PRId32,last.session,last.seq,last.voltage_mv);
    /* Replace a still-open viewer. Freeing the old session must not clear the
       new viewer or publisher. The new connection must receive cached data. */
    if (!open_viewer(&second)) { failure="replacement viewer handshake"; goto done; }
    if (!read_snapshot(&second,&initial,1500) || !initial.connected || initial.data.session!=last.session) {
        failure="replacement viewer initial snapshot"; goto done;
    }
    close_viewer(&first);
    if (!read_live(&second,6,&last)) { failure="live snapshots after old viewer is released"; goto done; }
done:
    close_viewer(&first); close_viewer(&second);
    if (failure) ESP_LOGE(TAG,"FAIL: %s",failure);
    else ESP_LOGI(TAG,"PASS: real HTTPD handshake, 18 live snapshots, viewer replacement/reconnect; seq=%"PRIu32" input_mV=%"PRId32,last.seq,last.voltage_mv);
    vTaskDelete(NULL);
}
void hud_server_selftest_start(void)
{
    if (xTaskCreate(check_task,"hud_selftest",8192,NULL,2,NULL)!=pdPASS)
        ESP_LOGE(TAG,"FAIL: cannot create diagnostic task");
}
