#include "hud_server.h"
#include "../../shared/hud_protocol.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

/* All cache, socket and session access stays on the port-80 HTTP server task. */
typedef struct { int fd; bool publisher; } hud_session_t;
static hud_session_t *publisher,*viewer;
static httpd_handle_t server_handle;
static hud_sample_t latest;
static bool received;
static int64_t received_us;
static uint32_t accepted,rejected;
static int64_t last_log_us;

static void release_session(void *ctx)
{
    if (publisher==ctx) publisher=NULL;
    if (viewer==ctx) viewer=NULL;
    free(ctx);
}
static int make_view(char *body,size_t capacity)
{
    int64_t elapsed=received ? (esp_timer_get_time()-received_us)/1000 : 0;
    hud_sample_t current=received ? latest : hud_empty();
    current.session=received ? latest.session : 1;
    hud_advance_age(&current,(uint64_t)elapsed);
    char data[HUD_MAX_JSON];
    if (hud_encode(&current,data,sizeof(data))<0) return -1;
    int n=snprintf(body,capacity,"{\"connected\":%s,\"uplink_age_ms\":%"PRId32",\"accepted\":%"PRIu32",\"rejected\":%"PRIu32",\"data\":%s}",
        publisher && received && elapsed<HUD_LINK_STALE_MS ? "true" : "false",
        received ? hud_age(elapsed) : -1,accepted,rejected,data);
    return n>=0 && (size_t)n<capacity ? n : -1;
}
static esp_err_t send_view(void)
{
    if (!viewer) return ESP_OK;
    char body[HUD_MAX_JSON+160]; int length=make_view(body,sizeof(body));
    if (length<0) return ESP_ERR_NO_MEM;
    httpd_ws_frame_t frame={.type=HTTPD_WS_TYPE_TEXT,.payload=(uint8_t *)body,.len=(size_t)length};
    esp_err_t err=httpd_ws_send_frame_async(server_handle,viewer->fd,&frame);
    if (err!=ESP_OK) httpd_sess_trigger_close(server_handle,viewer->fd);
    return err;
}
/* IDF >= 6.0.1 does not call the frame handler during the handshake. */
static esp_err_t connected(httpd_req_t *req)
{
    bool is_publisher=req->user_ctx!=NULL;
#ifdef CAMERA_HUD_SELFTEST
    /* Reserve the viewer slot for the diagnostic client in test firmware only,
       so a phone's automatic reconnect cannot invalidate the replacement test. */
    if (!is_publisher) {
        struct sockaddr_storage peer={0}; socklen_t length=sizeof(peer);
        if (getpeername(httpd_req_to_sockfd(req),(struct sockaddr *)&peer,&length)!=0) return ESP_FAIL;
        bool local=peer.ss_family==AF_INET && ((struct sockaddr_in *)&peer)->sin_addr.s_addr==htonl(INADDR_LOOPBACK);
        if (peer.ss_family==AF_INET6) {
            const struct in6_addr *address=&((struct sockaddr_in6 *)&peer)->sin6_addr;
            const uint8_t localhost[4]={127,0,0,1};
            local=IN6_IS_ADDR_LOOPBACK(address) || (IN6_IS_ADDR_V4MAPPED(address) && !memcmp(address->s6_addr+12,localhost,4));
        }
        if (!local) return ESP_FAIL;
    }
#endif
    hud_session_t *session=calloc(1,sizeof(*session));
    if (!session) return ESP_ERR_NO_MEM;
    session->fd=httpd_req_to_sockfd(req); session->publisher=is_publisher;
    req->sess_ctx=session; req->free_ctx=release_session;
    hud_session_t **slot=is_publisher ? &publisher : &viewer;
    if (*slot) httpd_sess_trigger_close(req->handle,(*slot)->fd);
    *slot=session;
    if (is_publisher) { received=false; latest=hud_empty(); }
    /* Bound a stalled viewer; never let it hold upload ACKs for seconds. */
    struct timeval timeout={.tv_sec=0,.tv_usec=100000};
    int no_delay=1;
    setsockopt(session->fd,IPPROTO_TCP,TCP_NODELAY,&no_delay,sizeof(no_delay));
    setsockopt(session->fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    setsockopt(session->fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    ESP_LOGI("hud","%s connected",is_publisher ? "controller" : "viewer");
    if (!is_publisher) return send_view();
    return ESP_OK;
}
static esp_err_t websocket(httpd_req_t *req)
{
    bool is_publisher=req->user_ctx!=NULL;
    if (!req->sess_ctx || (is_publisher && req->sess_ctx!=publisher) || (!is_publisher && req->sess_ctx!=viewer)) return ESP_FAIL;
    httpd_ws_frame_t frame={0};
    esp_err_t err=httpd_ws_recv_frame(req,&frame,0);
    if (err!=ESP_OK) return err;
    /* Built-in HTTPD handles ping/pong/close. Only complete text uploads are accepted. */
    if (!is_publisher || frame.type!=HTTPD_WS_TYPE_TEXT || !frame.final || frame.len==0 || frame.len>=HUD_MAX_JSON) {
        ++rejected; return ESP_FAIL;
    }
    char data[HUD_MAX_JSON]; frame.payload=(uint8_t *)data;
    err=httpd_ws_recv_frame(req,&frame,sizeof(data)-1);
    if (err!=ESP_OK) return err;
    hud_sample_t sample;
    if (!hud_decode(data,frame.len,&sample) || (received &&
        (sample.session!=latest.session || !hud_sequence_newer(sample.seq,latest.seq)))) {
        ++rejected; return ESP_FAIL;
    }
    latest=sample; received=true; received_us=esp_timer_get_time(); ++accepted;
    if (received_us-last_log_us>=5000000) {
        ESP_LOGI("hud","received=%"PRIu32" seq=%"PRIu32" voltage_mV=%"PRId32" spo2_x10=%"PRId32" pitch_cdeg=%"PRId32" roll_cdeg=%"PRId32" distance_cm=%"PRId32" aircraft_age_ms=%"PRId32,
            accepted,sample.seq,sample.voltage_mv,sample.spo2_x10,sample.pitch_cdeg,sample.roll_cdeg,sample.distance_cm,
            sample.air_age_ms==HUD_NA ? -1 : sample.air_age_ms);
        last_log_us=received_us;
    }
    char ack[80]; int length=snprintf(ack,sizeof(ack),"{\"ack\":%"PRIu32",\"session\":%"PRIu32"}",sample.seq,sample.session);
    httpd_ws_frame_t reply={.type=HTTPD_WS_TYPE_TEXT,.payload=(uint8_t *)ack,.len=(size_t)length};
    err=httpd_ws_send_frame(req,&reply);
    if (err==ESP_OK) (void)send_view();
    return err;
}
static esp_err_t state(httpd_req_t *req)
{
    char body[HUD_MAX_JSON+160]; int len=make_view(body,sizeof(body));
    if (len<0) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,body,len);
}
esp_err_t hud_server_register(httpd_handle_t server)
{
    server_handle=server; latest=hud_empty();
    const httpd_uri_t routes[]={
        {.uri="/ws/upload",.method=HTTP_GET,.handler=websocket,.is_websocket=true,.user_ctx=(void *)1,.ws_post_handshake_cb=connected},
        {.uri="/ws/view",.method=HTTP_GET,.handler=websocket,.is_websocket=true,.ws_post_handshake_cb=connected},
        {.uri="/api/telemetry",.method=HTTP_GET,.handler=state},
    };
    for (unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);++i) {
        esp_err_t err=httpd_register_uri_handler(server,&routes[i]); if (err!=ESP_OK) return err;
    }
    return ESP_OK;
}
