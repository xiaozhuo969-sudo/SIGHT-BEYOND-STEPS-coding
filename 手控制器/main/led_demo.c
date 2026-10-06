#include <stdbool.h>
#include <stdint.h>

#include "button_gpio.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "led_demo.h"
#include "telemetry.h"

// H5: GPIO9 palm; former LED1/LED2 are now flight buttons on GPIO10/11.
// The remaining LEDs turn on at GPIO low; flight buttons read high at rest.
#define PALM_BUTTON_GPIO GPIO_NUM_9
#define START_BUTTON_GPIO GPIO_NUM_10
#define STOP_BUTTON_GPIO GPIO_NUM_11
#define FLIGHT_BUTTON_ACTIVE_LEVEL 0
#define ALARM_LED_GPIO GPIO_NUM_5
#define LED_COUNT 5
#define STEP_TIME_MS 150

static const gpio_num_t s_led_pins[LED_COUNT] = {
    GPIO_NUM_12, GPIO_NUM_13,
    GPIO_NUM_14, GPIO_NUM_21, GPIO_NUM_47,
};
static const char *TAG = "led_demo";

bool flight_stop_pressed(void)
{
    return gpio_get_level(STOP_BUTTON_GPIO)==FLIGHT_BUTTON_ACTIVE_LEVEL;
}

unsigned flight_keys_raw_levels(void)
{
    return (unsigned)gpio_get_level(START_BUTTON_GPIO) |
           ((unsigned)gpio_get_level(STOP_BUTTON_GPIO)<<1);
}

static void flight_key_down(void *button, void *user_data)
{
    (void)button;
    telemetry_flight_key((unsigned)(uintptr_t)user_data,true);
}

static void flight_key_up(void *button, void *user_data)
{
    (void)button;
    // A simultaneous stop has priority even before its debounce callback.
    if(flight_stop_pressed()) telemetry_flight_key(1,true);
    telemetry_flight_key((unsigned)(uintptr_t)user_data,false);
}

typedef enum {
    NEXT_PATTERN,
    TOGGLE_LEDS,
} led_action_t;

static QueueHandle_t s_action_queue;

static void show_leds(uint8_t mask)
{
    for (unsigned i = 0; i < LED_COUNT; ++i) {
        gpio_set_level(s_led_pins[i], (mask & (1U << i)) ? 0 : 1);
    }
}

static uint8_t pattern_frame(unsigned pattern, unsigned phase)
{
    if (pattern == 0) {  // One light moves from the first finger to the last.
        return (uint8_t)(1U << phase);
    }
    if (pattern == 1) {  // One light bounces back and forth.
        unsigned pos = phase < LED_COUNT ? phase : 2 * LED_COUNT - 2 - phase;
        return (uint8_t)(1U << pos);
    }
    // Fill from the first finger, then clear from the last.
    unsigned on_count = phase < LED_COUNT ? phase + 1 : 2 * LED_COUNT - 1 - phase;
    return (uint8_t)((1U << on_count) - 1U);
}

static unsigned pattern_length(unsigned pattern)
{
    return pattern == 0 ? LED_COUNT : pattern == 1 ? 2 * LED_COUNT - 2 : 2 * LED_COUNT;
}

static void button_short_cb(void *button, void *user_data)
{
    (void)button;
    (void)user_data;
    const led_action_t action = NEXT_PATTERN;
    xQueueSend(s_action_queue, &action, 0);
}

static void button_long_cb(void *button, void *user_data)
{
    (void)button;
    (void)user_data;
    const led_action_t action = TOGGLE_LEDS;
    xQueueSend(s_action_queue, &action, 0);
}

static void button_down_cb(void *button, void *user_data)
{
    (void)button;
    (void)user_data;
    telemetry_set_button(true);
}

static void button_up_cb(void *button, void *user_data)
{
    (void)button;
    (void)user_data;
    telemetry_set_button(false);
}

static void led_task(void *arg)
{
    (void)arg;
    unsigned pattern = 0;
    unsigned phase = 0;
    bool enabled = true;
    const char *names[] = {"forward", "bounce", "fill/clear"};
    ESP_LOGI(TAG, "LED mode on, pattern: %s", names[pattern]);
    telemetry_set_led(enabled, pattern);
    while (true) {
        led_action_t action;
        if (xQueueReceive(s_action_queue, &action, pdMS_TO_TICKS(STEP_TIME_MS)) == pdTRUE) {
            if (action == NEXT_PATTERN) {
                if (enabled) {
                    pattern = (pattern + 1) % 3;
                    phase = 0;
                    ESP_LOGI(TAG, "pattern: %s", names[pattern]);
                }
            } else {
                enabled = !enabled;
                phase = 0;
                ESP_LOGI(TAG, "LED mode %s", enabled ? "on" : "off");
            }
            if (!enabled) {
                telemetry_set_led(enabled, pattern);
                show_leds(0);
                continue;
            }
            telemetry_set_led(enabled, pattern);
        }
        if (enabled) {
            show_leds(pattern_frame(pattern, phase));
            phase = (phase + 1) % pattern_length(pattern);
        }
    }
}

void led_demo_start(void)
{
    // The separate H6 obstacle alarm lamp is not part of the running patterns.
    ESP_ERROR_CHECK(gpio_set_level(ALARM_LED_GPIO, 1));
    uint64_t led_mask = 1ULL << ALARM_LED_GPIO;
    for (unsigned i = 0; i < LED_COUNT; ++i) {
        ESP_ERROR_CHECK(gpio_set_level(s_led_pins[i], 1));
        led_mask |= 1ULL << s_led_pins[i];
    }
    const gpio_config_t output_config = {
        .pin_bit_mask = led_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&output_config));
    show_leds(0);

    // LED1/LED2 read high when released (verified on the assembled controller).
    // Use low-active inputs like LED0/palm; retain the existing pull-down wiring.
    // Never drive GPIO10/11 as outputs.
    const gpio_num_t key_pins[2]={START_BUTTON_GPIO,STOP_BUTTON_GPIO};
    button_handle_t flight_buttons[2]={0};
    const button_config_t key_config={.short_press_time=50,.long_press_time=1200};
    for(unsigned i=0;i<2;++i) {
        const button_gpio_config_t gpio_key={.gpio_num=key_pins[i],.active_level=FLIGHT_BUTTON_ACTIVE_LEVEL,.disable_pull=true};
        ESP_ERROR_CHECK(iot_button_new_gpio_device(&key_config,&gpio_key,&flight_buttons[i]));
        ESP_ERROR_CHECK(gpio_set_pull_mode(key_pins[i],GPIO_PULLDOWN_ONLY));
    }
    telemetry_flight_keys_init(gpio_get_level(START_BUTTON_GPIO)==FLIGHT_BUTTON_ACTIVE_LEVEL,flight_stop_pressed());
    for(unsigned i=0;i<2;++i) {
        ESP_ERROR_CHECK(iot_button_register_cb(flight_buttons[i],BUTTON_PRESS_DOWN,NULL,flight_key_down,(void *)(uintptr_t)i));
        ESP_ERROR_CHECK(iot_button_register_cb(flight_buttons[i],BUTTON_PRESS_UP,NULL,flight_key_up,(void *)(uintptr_t)i));
    }
    ESP_LOGI(TAG,"LED1/GPIO10: connect+takeoff on release; LED2/GPIO11: immediate stop (active low)");

    s_action_queue = xQueueCreate(8, sizeof(led_action_t));
    ESP_ERROR_CHECK(s_action_queue ? ESP_OK : ESP_ERR_NO_MEM);

    // The assembled palm button measured high at rest and low when pressed.
    // A pull-down keeps GPIO9 low when the switch opens.
    const button_config_t button_config = {
        .long_press_time = 1200,
        .short_press_time = 50,
    };
    const button_gpio_config_t button_gpio = {
        .gpio_num = PALM_BUTTON_GPIO,
        .active_level = 0,
        .disable_pull = true,
    };
    button_handle_t button = NULL;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&button_config, &button_gpio, &button));
    ESP_ERROR_CHECK(gpio_set_pull_mode(PALM_BUTTON_GPIO, GPIO_PULLDOWN_ONLY));
    ESP_ERROR_CHECK(iot_button_register_cb(button, BUTTON_SINGLE_CLICK, NULL, button_short_cb, NULL));
    ESP_ERROR_CHECK(iot_button_register_cb(button, BUTTON_LONG_PRESS_START, NULL, button_long_cb, NULL));
    ESP_ERROR_CHECK(iot_button_register_cb(button, BUTTON_PRESS_DOWN, NULL, button_down_cb, NULL));
    ESP_ERROR_CHECK(iot_button_register_cb(button, BUTTON_PRESS_UP, NULL, button_up_cb, NULL));
    ESP_ERROR_CHECK(xTaskCreate(led_task, "led_demo", 3072, NULL, 5, NULL) == pdPASS
                        ? ESP_OK : ESP_ERR_NO_MEM);
}
