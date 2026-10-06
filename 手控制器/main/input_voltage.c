#include "input_voltage.h"
#include <inttypes.h>
#include "sdkconfig.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG="input_voltage";
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static int32_t voltage_mv=HUD_NA;
static int64_t measured_us;

void input_voltage_snapshot(hud_sample_t *sample)
{
    portENTER_CRITICAL(&mux);
    int32_t mv=voltage_mv; int64_t at=measured_us;
    portEXIT_CRITICAL(&mux);
    if (at && mv!=HUD_NA) {
        sample->voltage_mv=mv;
        sample->voltage_age_ms=hud_age((esp_timer_get_time()-at)/1000);
    }
}
static void voltage_task(void *arg)
{
    (void)arg;
    adc_oneshot_unit_handle_t adc=NULL;
    adc_cali_handle_t calibration=NULL;
    /* XFJ (2).pdf: VCC_IN -> R7 10k -> R10 10k -> GND;
       midpoint -> R9 499 ohm -> ADC0/GPIO15 (ADC2 channel 4).
       VCC_IN may be selected USB supply: report voltage, never infer charge. */
    adc_oneshot_unit_init_cfg_t unit={.unit_id=ADC_UNIT_2};
    adc_oneshot_chan_cfg_t channel={.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_DEFAULT};
    adc_cali_curve_fitting_config_t cal={.unit_id=ADC_UNIT_2,.chan=ADC_CHANNEL_4,
        .atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_DEFAULT};
    esp_err_t err=adc_oneshot_new_unit(&unit,&adc);
    if (err==ESP_OK) err=adc_oneshot_config_channel(adc,ADC_CHANNEL_4,&channel);
    if (err==ESP_OK) err=adc_cali_create_scheme_curve_fitting(&cal,&calibration);
    if (err!=ESP_OK) {
        ESP_LOGE(TAG,"Calibrated GPIO15 sampling unavailable: %s",esp_err_to_name(err));
        if (adc) adc_oneshot_del_unit(adc);
        vTaskDelete(NULL); return;
    }
    ESP_LOGI(TAG,"One-point voltage gain: %d / %d (reference / uncorrected mV)",
        CONFIG_HAND_VOLTAGE_REFERENCE_MV,CONFIG_HAND_VOLTAGE_READING_MV);
    unsigned failures=0;
    int64_t last_log=0;
    for (;;) {
        int total=0,raw_total=0,valid=0;
        /* Average one short acquisition only, without smoothing away changes. */
        for (unsigned i=0;i<16;++i) {
            int raw=0,mv=0;
            err=adc_oneshot_read(adc,ADC_CHANNEL_4,&raw);
            if (err==ESP_OK && adc_cali_raw_to_voltage(calibration,raw,&mv)==ESP_OK && raw>0 && raw<4095) {
                total+=mv; raw_total+=raw; ++valid;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        int32_t divider_mv=valid>=8 ? (total*2+valid/2)/valid : HUD_NA;
        /* User reference on 2026-10-03: 5130 mV displayed at 4140 mV battery.
           Apply a proportional gain to every new sample; retain invalids. */
        int32_t corrected_mv=divider_mv==HUD_NA ? HUD_NA : (int32_t)(
            ((int64_t)divider_mv*CONFIG_HAND_VOLTAGE_REFERENCE_MV+
             CONFIG_HAND_VOLTAGE_READING_MV/2)/CONFIG_HAND_VOLTAGE_READING_MV);
        int64_t now=esp_timer_get_time();
        portENTER_CRITICAL(&mux);
        voltage_mv=corrected_mv;
        measured_us=now;
        portEXIT_CRITICAL(&mux);
        if (!last_log || now-last_log>=5000000) {
            ESP_LOGI(TAG,"raw_avg=%d pin_mV=%d divider_mV=%"PRId32" corrected_mV=%"PRId32" valid=%d/16",
                valid ? raw_total/valid : -1,valid ? (total+valid/2)/valid : -1,
                divider_mv==HUD_NA ? -1 : divider_mv,corrected_mv==HUD_NA ? -1 : corrected_mv,valid);
            last_log=now;
        }
        if (valid<8 && ++failures%10==1) ESP_LOGW(TAG,"ADC2 busy/invalid: %d/16 samples; voltage unavailable",valid);
        else if (valid>=8) failures=0;
        vTaskDelay(pdMS_TO_TICKS(950));
    }
}
void input_voltage_start(void)
{
    if (xTaskCreate(voltage_task,"input_voltage",3072,NULL,2,NULL)!=pdPASS)
        ESP_LOGE(TAG,"No memory for input voltage task");
}
