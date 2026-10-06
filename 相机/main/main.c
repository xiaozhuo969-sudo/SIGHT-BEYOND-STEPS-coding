#include "app.h"
#include "esp_log.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "nvs_flash.h"
#ifdef CAMERA_HUD_SELFTEST
void hud_server_selftest_start(void);
#endif
void app_main(void)
{
    // Keep the existing NVS partition intact; Wi-Fi calibration still uses it.
    ESP_ERROR_CHECK(nvs_flash_init());
    uint32_t flash = 0;
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash));
    ESP_LOGI("monitor", "Flash: %lu MiB; initialized PSRAM: %u MiB",
             (unsigned long)(flash / 1048576), (unsigned)(esp_psram_get_size() / 1048576));
    network_start();
    camera_server_start();
#ifdef CAMERA_HUD_SELFTEST
    hud_server_selftest_start();
#endif
}
