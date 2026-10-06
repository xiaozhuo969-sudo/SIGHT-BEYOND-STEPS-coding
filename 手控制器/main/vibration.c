#include <stdint.h>

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "telemetry.h"
#include "vibration.h"

// XFJ (2).pdf: MOT0=GPIO18, MOT1=GPIO17. High drives the Darlington,
// pulls the AO3401A P-MOS gate low and switches motor power on.
#define MOTOR_COUNT 2
#define MAX_PULSE_MS 2000
#define PWM_MAX 255

static const char *TAG = "vibration";
static const int s_pins[MOTOR_COUNT] = {18, 17};
static SemaphoreHandle_t s_lock;
static int64_t s_deadline_us[MOTOR_COUNT];
static unsigned s_strength[MOTOR_COUNT];

static esp_err_t set_motor(unsigned channel, unsigned strength)
{
    const uint32_t duty = (strength * PWM_MAX + 50) / 100;
    // All changes to a channel are serialized by s_lock. The combined
    // set_duty_and_update API needs the fade service, which this demo does not use.
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel, duty);
    if (err == ESP_OK) {
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel);
    }
    if (err == ESP_OK) {
        s_strength[channel] = strength;
        telemetry_set_motor(channel, strength);
    }
    return err;
}

esp_err_t vibration_pulse(unsigned channel, unsigned strength, unsigned duration_ms)
{
    if (channel >= MOTOR_COUNT || strength > 100 || duration_ms > MAX_PULSE_MS ||
        (strength != 0 && duration_ms == 0) || s_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = set_motor(channel, strength);
    if (err == ESP_OK) {
        s_deadline_us[channel] = strength ? esp_timer_get_time() + duration_ms * 1000LL : 0;
    }
    xSemaphoreGive(s_lock);
    return err;
}

static void vibration_watchdog(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            const int64_t now = esp_timer_get_time();
            for (unsigned i = 0; i < MOTOR_COUNT; ++i) {
                if (s_strength[i] && now >= s_deadline_us[i]) {
                    s_deadline_us[i] = 0;
                    if (set_motor(i, 0) != ESP_OK) {
                        ESP_LOGW(TAG, "failed to stop motor %u", i);
                    }
                }
            }
            xSemaphoreGive(s_lock);
        }
    }
}

void vibration_start(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    for (unsigned i = 0; i < MOTOR_COUNT; ++i) {
        const ledc_channel_config_t output = {
            .gpio_num = s_pins[i],
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = (ledc_channel_t)i,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&output));
    }
    s_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_lock ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(vibration_watchdog, "motor_watchdog", 2560,
                                NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "MOT0 GPIO18 and MOT1 GPIO17 ready; 2 s pulse limit");
}
