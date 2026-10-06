#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_health_spo2.h"
#include "esp_max30102.h"
#include "max30102_demo.h"
#include "telemetry.h"
#include "ppg_window.h"

// XFJ (2).pdf H2: GPIO39=SDA1, GPIO38=SCL1, GPIO40=INT1, 3.3 V and GND.
#define MAX30102_SDA_GPIO GPIO_NUM_39
#define MAX30102_SCL_GPIO GPIO_NUM_38
#define MAX30102_I2C_PORT I2C_NUM_1
#define MAX30102_PART_ID 0x15
#define CHUNK_SAMPLES 32
#define TASK_STACK_BYTES 8192

static const char *TAG = "max30102_demo";
static ppg_window_t s_window;

static void max30102_task(void *arg)
{
    (void)arg;
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = MAX30102_I2C_PORT,
        .sda_io_num = MAX30102_SDA_GPIO,
        .scl_io_num = MAX30102_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO39/38 I2C bus init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    const i2c_MAX30102_config_t sensor_config = {
        .MAX30102_device = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = I2C_DEVICE_ADDR,
            .scl_speed_hz = 100000,
        },
    };
    i2c_MAX30102_handle_t sensor = NULL;
    err = i2c_MAX30102_init(bus, &sensor_config, &sensor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 I2C device init failed: %s", esp_err_to_name(err));
        i2c_del_master_bus(bus);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "MAX30102 H2: SDA=GPIO39 SCL=GPIO38 INT=GPIO40 addr=0x57");
    uint8_t part_id = 0;
    for (;;) {
        err = i2c_MAX30102_read_reg(sensor, REG_PART_ID, &part_id, 1);
        if (err == ESP_OK && part_id == MAX30102_PART_ID) {
            break;
        }
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "unexpected part ID 0x%02X (expected 0x15)", part_id);
        } else {
            ESP_LOGW(TAG, "sensor not detected at 0x57; check H2 wiring/power");
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    err = maxim_max30102_reset(sensor);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        err = esp_MAX30102_set(sensor);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 configuration failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    esp_health_spo2_cfg_t spo2_config = ESP_HEALTH_SPO2_CFG_DEFAULT();
    // Use the library defaults (correlation 0.8 / periodicity 0.5).
    // The relaxed 0.55 / 0.30 example gates admitted unstable contact traces.
    esp_health_spo2_handle_t spo2 = NULL;
    esp_health_err_t health_err = esp_health_spo2_open(&spo2_config, &spo2);
    if (health_err != ESP_HEALTH_ERR_OK || spo2 == NULL) {
        ESP_LOGE(TAG, "SpO2 processor init failed: %d", (int)health_err);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "part ID=0x%02X ready; streaming FIFO, rolling SpO2 (no 500-sample wait)", part_id);
    telemetry_set_ppg(true, false, 0, 0, 0);
    int finger_off = 1;
    bool off_logged = false;
    uint32_t red[CHUNK_SAMPLES], ir[CHUNK_SAMPLES];
    TickType_t last_sample = xTaskGetTickCount(), last_log = 0, last_error_log = 0;
    bool failed = false;
    ppg_contact_t contact = {0};
    uint32_t contact_resets = 0;

    for (;;) {
        int got = 0;
        err = i2c_MAX30102_read_spo2_ppg(sensor, MAX30102_INT_GPIO,
                                      &finger_off, red, ir, CHUNK_SAMPLES, &got);
        TickType_t now = xTaskGetTickCount();
        if (err != ESP_OK || (!got && now-last_sample >= pdMS_TO_TICKS(250))) {
            if (!failed) {
                telemetry_set_ppg(true, false, 0, 0, 0);
                telemetry_set_spo2(false, false, 0, 0, false);
                s_window.count = 0; finger_off = 1;
                contact = (ppg_contact_t){0};
                telemetry_set_ppg_quality(0, 0, contact_resets);
                esp_health_spo2_reset(spo2);
                failed = true;
            }
            if (!last_error_log || now-last_error_log >= pdMS_TO_TICKS(2000)) {
                ESP_LOGW(TAG, "FIFO stream unavailable: %s", got ? esp_err_to_name(err) :
                         err == ESP_OK ? "no new samples for 250ms" : esp_err_to_name(err));
                last_error_log = now;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!got) { vTaskDelay(1); continue; }
        failed = false; last_sample = now;
        // Publish the latest actual pair, without averaging a 32-sample batch.
        telemetry_set_ppg(true, true, red[got-1], ir[got-1], (unsigned)got);
        if (finger_off) {
            telemetry_set_spo2(false, false, 0, 0, false);
            if (!off_logged) {
                ESP_LOGI(TAG, "finger off; raw Red/IR streaming, SpO2=--");
                off_logged = true;
            }
            s_window.count = 0;
            contact = (ppg_contact_t){0};
            telemetry_set_ppg_quality(0, 0, contact_resets);
            esp_health_spo2_reset(spo2);
        } else {
            if (off_logged) {
                ESP_LOGI(TAG, "finger detected; computing on every new FIFO read");
                off_logged = false;
            }
            for (int i=0; i<got; ++i) {
                if (ppg_contact_step(&contact, red[i], ir[i])) {
                    s_window.count = 0; ++contact_resets;
                    esp_health_spo2_reset(spo2);
                    continue;
                }
                ppg_window_append(&s_window, red+i, ir+i, 1);
            }
            esp_health_spo2_result_t result = {0};
            // The existing processor accepts >=2 samples and rejects windows
            // without sufficient pulse evidence. Never replace invalid with 0%.
            health_err = s_window.count >= 2 ?
                esp_health_spo2_process(spo2, s_window.ir, s_window.red,
                                        (int)s_window.count, &result) : ESP_HEALTH_ERR_INVALID_PARAMETER;
            bool valid = health_err == ESP_HEALTH_ERR_OK && result.spo2 >= 70 && result.spo2 <= 100;
            telemetry_set_ppg_quality(result.correlation, result.quality_ratio, contact_resets);
            telemetry_set_spo2(true, valid, result.spo2, (int)s_window.count, valid);
            if (now-last_log >= pdMS_TO_TICKS(1000)) {
                ESP_LOGI(TAG, "stream: Red=%lu IR=%lu history=%u SpO2=%.1f valid=%u corr=%.2f quality=%.2f contact_resets=%lu",
                         (unsigned long)red[got-1], (unsigned long)ir[got-1], s_window.count,
                         valid ? result.spo2 : 0, valid, result.correlation, result.quality_ratio,
                         (unsigned long)contact_resets);
                last_log = now;
            }
        }
        // Yield to the existing radio/control tasks; no batch accumulation wait.
        vTaskDelay(1);
    }

}

void max30102_demo_start(void)
{
    if (xTaskCreate(max30102_task, "max30102", TASK_STACK_BYTES, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to start MAX30102 task");
    }
}
