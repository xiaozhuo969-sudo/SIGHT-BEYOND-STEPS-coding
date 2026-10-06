// Capture OV2640 hardware JPEG frames with espressif/esp32-camera.
#include "app.h"
#include "board.h"
#include "hud_server.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "lwip/sockets.h"

extern const char index_start[] asm("_binary_index_html_start");
static atomic_int camera_error = ESP_FAIL;
static framesize_t current_size = FRAMESIZE_QVGA;
static esp_err_t init_camera(framesize_t size, int quality);
static atomic_bool streaming;
static atomic_uint frame_count, encode_ms, capture_ms, send_ms, frame_ms;
#define JPEG_CAP (160 * 1024)
#define JPEG_PREFIX 128
static uint8_t *jpeg;
static bool jpeg_header_valid(const camera_fb_t *fb)
{
    if (!fb || fb->format != PIXFORMAT_JPEG || fb->len < 4 || fb->len > JPEG_CAP)
        return false;
    const uint8_t *data = fb->buf;
    size_t length = fb->len, offset = 2;
    if (data[0] != 0xff || data[1] != 0xd8 ||
        data[length - 2] != 0xff || data[length - 1] != 0xd9) return false;
    // Validate header segment bounds through SOS. Pixel decoding remains on
    // the viewer; this catches the previously observed damaged APP0 length.
    while (offset < length - 2) {
        if (data[offset++] != 0xff) return false;
        while (offset < length - 2 && data[offset] == 0xff) offset++;
        if (offset >= length - 2) return false;
        uint8_t marker = data[offset++];
        if (!marker || marker == 0xd8 || marker == 0xd9 || offset + 2 > length - 2)
            return false;
        size_t segment = ((size_t)data[offset] << 8) | data[offset + 1];
        if (segment < 2 || segment > length - 2 - offset) return false;
        if (marker == 0xda) return true;
        offset += segment;
    }
    return false;
}
static esp_err_t page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, index_start);
}
static esp_err_t status(httpd_req_t *req)
{
    char ip[16]; int clients;
    network_status(ip, sizeof(ip), &clients);
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(root, "mode", "hotspot");
    cJSON_AddStringToObject(root, "sensor", "OV2640");
    cJSON_AddStringToObject(root, "pixel_format", "JPEG");
    cJSON_AddStringToObject(root, "stream_format", "JPEG");
    cJSON_AddStringToObject(root, "ssid", network_ap_name());
    cJSON_AddStringToObject(root, "ip", ip);
    cJSON_AddNumberToObject(root, "clients", clients);
    cJSON_AddBoolToObject(root, "camera", camera_error == ESP_OK);
    cJSON_AddStringToObject(root, "camera_error", esp_err_to_name(camera_error));
    cJSON_AddNumberToObject(root, "psram", esp_psram_get_size());
    cJSON_AddNumberToObject(root, "frames", atomic_load(&frame_count));
    cJSON_AddNumberToObject(root, "encode_ms", atomic_load(&encode_ms));
    cJSON_AddNumberToObject(root, "capture_ms", atomic_load(&capture_ms));
    cJSON_AddNumberToObject(root, "send_ms", atomic_load(&send_ms));
    cJSON_AddNumberToObject(root, "frame_ms", atomic_load(&frame_ms));
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, body);
    free(body); return err;
}
static esp_err_t client_error(httpd_req_t *req)
{
    char body[384];
    if (req->content_len <= 0 || req->content_len >= sizeof(body))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid report");
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) return ESP_FAIL;
        received += n;
    }
    body[received] = 0;
    cJSON *report = cJSON_Parse(body);
    const cJSON *stage = cJSON_GetObjectItemCaseSensitive(report, "stage");
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(report, "message");
    if (cJSON_IsString(stage) && cJSON_IsString(message))
        ESP_LOGW("viewer", "Client error at %.24s: %.200s", stage->valuestring, message->valuestring);
    cJSON_Delete(report);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}
static esp_err_t stream_send_all(httpd_req_t *req, const char *data, size_t length)
{
    while (length) {
        int sent = httpd_socket_send(req->handle, httpd_req_to_sockfd(req), data, length, 0);
        if (sent <= 0) return ESP_FAIL;
        data += sent; length -= sent;
    }
    return ESP_OK;
}

static void stream_task(void *arg)
{
    httpd_req_t *req = arg;
    httpd_handle_t server = req->handle;
    int socket_fd = httpd_req_to_sockfd(req);
    // Every frame is one bulk write, without tiny transfer-chunk packets.
    int no_delay = 0;
    if (setsockopt(httpd_req_to_sockfd(req), IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)) != 0)
        ESP_LOGW("camera", "Could not set TCP_NODELAY");
    char query[64] = "", value[12] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    httpd_query_key_value(query, "size", value, sizeof(value));
    framesize_t size = !strcmp(value, "vga") ? FRAMESIZE_VGA : FRAMESIZE_QVGA;
    char quality[12] = "";
    httpd_query_key_value(query, "quality", quality, sizeof(quality));
    char format[12] = "";
    httpd_query_key_value(query, "format", format, sizeof(format));
    bool byte_stream = !strcmp(format, "bytes");
    // OV2640 hardware JPEG quality is 0..63; lower means more detail.
    int jpeg_quality = !strcmp(quality, "fine") ? 12 : 20;
    // Reinitialize DMA geometry when changing resolution.
    if (size != current_size) {
        esp_camera_deinit();
        camera_error = init_camera(size, jpeg_quality);
        if (camera_error == ESP_OK) current_size = size;
    } else {
        sensor_t *sensor = esp_camera_sensor_get();
        if (!sensor || sensor->set_quality(sensor, jpeg_quality) != 0)
            camera_error = ESP_FAIL;
    }
    if (camera_error != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Resolution failed");
        goto done;
    }
    // Flush queued frames captured before changing quality/resolution.
    for (int i = 0; i < 2; ++i) {
        camera_fb_t *warmup = esp_camera_fb_get();
        if (warmup) esp_camera_fb_return(warmup);
    }
    ESP_LOGI("camera", "Stream: OV2640 hardware JPEG %s, quality=%d, transport=%s", size == FRAMESIZE_VGA ? "640x480" : "320x240", jpeg_quality, byte_stream ? "bytes" : "mjpeg");
    char response_header[256];
    // WebKit cancels multipart responses requested by fetch rather than an
    // image element. The canvas viewer parses the same frame bytes itself.
    int response_length = snprintf(response_header, sizeof(response_header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n",
        byte_stream ? "application/octet-stream" : "multipart/x-mixed-replace;boundary=frame");
    // Close-delimited HTTP streaming avoids separate writes for the chunk size
    // and trailing CRLF. This socket is owned exclusively by the async worker.
    if (response_length <= 0 || response_length >= (int)sizeof(response_header) ||
        stream_send_all(req, response_header, response_length) != ESP_OK) goto done;
    unsigned sent_frames = 0;
    int64_t report_start = esp_timer_get_time();
    unsigned report_frames = 0;
    for (;;) {
        int64_t frame_start = esp_timer_get_time();
        camera_fb_t *fb = esp_camera_fb_get();
        atomic_store(&capture_ms, (esp_timer_get_time() - frame_start) / 1000);
        if (!fb) {
            ESP_LOGE("camera", "OV2640 frame capture failed");
            break;
        }
        if (!jpeg_header_valid(fb)) {
            ESP_LOGE("camera", "Invalid OV2640 JPEG header or size: %u bytes; check camera ribbon", (unsigned)fb->len);
            esp_camera_fb_return(fb);
            break;
        }
        size_t jpeg_size = fb->len;
        memcpy(jpeg + JPEG_PREFIX, fb->buf, jpeg_size);
        esp_camera_fb_return(fb);
        atomic_store(&encode_ms, 0); // No software JPEG encoding on ESP32.
        int64_t send_start = esp_timer_get_time();
        char header[128];
        int n = snprintf(header, sizeof(header), "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", (unsigned)jpeg_size);
        // Prefix space keeps multipart header and JPEG in one contiguous send.
        if (n <= 0 || n >= JPEG_PREFIX) break;
        memcpy(jpeg + JPEG_PREFIX - n, header, n);
        if (stream_send_all(req, (char *)jpeg + JPEG_PREFIX - n, jpeg_size + n) != ESP_OK) break;
        int64_t frame_end = esp_timer_get_time();
        atomic_store(&send_ms, (frame_end - send_start) / 1000);
        atomic_store(&frame_ms, (frame_end - frame_start) / 1000);
        atomic_fetch_add(&frame_count, 1);
        sent_frames++; report_frames++;
        if (sent_frames == 1)
            ESP_LOGI("camera", "OV2640 first JPEG: %u bytes, capture=%u ms", (unsigned)jpeg_size, atomic_load(&capture_ms));
        if (frame_end - report_start >= 5000000) {
            ESP_LOGI("camera", "OV2640 stream: %.1f fps, capture=%u ms, send=%u ms", report_frames * 1000000.0 / (frame_end - report_start), atomic_load(&capture_ms), atomic_load(&send_ms));
            report_start = frame_end; report_frames = 0;
        }
        vTaskDelay(1);
    }
done:
    httpd_req_async_handler_complete(req);
    httpd_sess_trigger_close(server, socket_fd);
    atomic_store(&streaming, false);
    vTaskDelete(NULL);
}
static esp_err_t unavailable(httpd_req_t *req, const char *message)
{
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_sendstr(req, message);
}
static esp_err_t stream(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (camera_error != ESP_OK) return unavailable(req, esp_err_to_name(camera_error));
    if (atomic_exchange(&streaming, true)) return unavailable(req, "Camera is in use; close the other viewer");
    httpd_req_t *async = NULL;
    esp_err_t err = httpd_req_async_handler_begin(req, &async);
    if (err != ESP_OK) { atomic_store(&streaming, false); return err; }
    if (xTaskCreatePinnedToCore(stream_task, "video", 8192, async, 4, NULL, 1) != pdPASS) {
        unavailable(async, "Insufficient memory");
        httpd_req_async_handler_complete(async);
        atomic_store(&streaming, false);
    }
    return ESP_OK;
}
static esp_err_t init_camera(framesize_t size, int quality)
{
        camera_config_t config = {
            .pin_pwdn = CAM_PWDN, .pin_reset = CAM_RESET,
            .pin_xclk = CAM_XCLK, .pin_sccb_sda = CAM_SDA, .pin_sccb_scl = CAM_SCL,
            .pin_d0 = CAM_D0, .pin_d1 = CAM_D1, .pin_d2 = CAM_D2, .pin_d3 = CAM_D3,
            .pin_d4 = CAM_D4, .pin_d5 = CAM_D5, .pin_d6 = CAM_D6, .pin_d7 = CAM_D7,
            .pin_vsync = CAM_VSYNC, .pin_href = CAM_HREF, .pin_pclk = CAM_PCLK,
            .xclk_freq_hz = 20000000, .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
            .pixel_format = PIXFORMAT_JPEG, .frame_size = size,
            .jpeg_quality = quality, .fb_count = 2, .fb_location = CAMERA_FB_IN_PSRAM,
            .grab_mode = CAMERA_GRAB_LATEST,
        };
        esp_err_t error = esp_camera_init(&config);
        if (error == ESP_OK && esp_camera_sensor_get()->id.PID != OV2640_PID) {
            ESP_LOGE("camera", "Expected OV2640, detected PID=0x%04x", esp_camera_sensor_get()->id.PID);
            esp_camera_deinit(); error = ESP_ERR_NOT_SUPPORTED;
        }
    return error;
}
void camera_server_start(void)
{
    if (esp_psram_get_size() >= 2 * 1024 * 1024) {
        camera_error = init_camera(current_size, 20);
        if (camera_error == ESP_OK) {
            jpeg = heap_caps_malloc(JPEG_CAP + JPEG_PREFIX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!jpeg) { esp_camera_deinit(); camera_error = ESP_ERR_NO_MEM; }
        }
    } else camera_error = ESP_ERR_NO_MEM;
    if (camera_error == ESP_OK) {
        // Inspect real sensor output over USB without changing computer Wi-Fi.
        for (int i = 0; i < 2; ++i) {
            camera_fb_t *warmup = esp_camera_fb_get();
            if (warmup) esp_camera_fb_return(warmup);
        }
        bool valid = false;
        for (int i = 0; i < 3 && !valid; ++i) {
            camera_fb_t *fb = esp_camera_fb_get();
            valid = jpeg_header_valid(fb);
            if (fb) {
                ESP_LOGI("camera", "OV2640 startup JPEG: %u bytes, header=%s", (unsigned)fb->len, valid ? "valid" : "invalid");
                if (!valid)
                    ESP_LOG_BUFFER_HEX_LEVEL("camera", fb->buf, fb->len < 32 ? fb->len : 32, ESP_LOG_WARN);
                esp_camera_fb_return(fb);
            }
        }
        if (!valid) {
            ESP_LOGE("camera", "OV2640 startup JPEG check failed; check camera ribbon");
            esp_camera_deinit();
            camera_error = ESP_ERR_INVALID_RESPONSE;
        }
    }
    ESP_LOGI("camera", "Camera: %s; OV2640 hardware JPEG, two frame buffers, max VGA", esp_err_to_name(camera_error));
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192; cfg.max_uri_handlers = 8;
    cfg.max_open_sockets = 6;
    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));
    httpd_uri_t routes[] = {
        {.uri="/", .method=HTTP_GET, .handler=page},
        {.uri="/api/status", .method=HTTP_GET, .handler=status},
        {.uri="/api/client-error", .method=HTTP_POST, .handler=client_error},
    };
    for (unsigned i = 0; i < sizeof(routes)/sizeof(routes[0]); ++i) ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    ESP_ERROR_CHECK(hud_server_register(server));
    cfg.server_port = 81; cfg.ctrl_port += 1; cfg.send_wait_timeout = 3;
    cfg.max_open_sockets = 2;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));
    httpd_uri_t video = {.uri="/stream", .method=HTTP_GET, .handler=stream};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &video));
}
