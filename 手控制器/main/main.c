#include "qmi8658.h"  // This vendor header defines M_PI; include it before math.h.
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_demo.h"
#include "max30102_demo.h"
#include "nrf24_demo.h"
#include "telemetry.h"
#include "vibration.h"
#include "input_voltage.h"
#include "camera_uplink.h"
#include "gyro_calibration.h"

// XFJ (2).pdf: U4 IO41/IO42 connect to U5 SDA0/SCL0; U5 SA0 is tied to GND.
#define IMU_SDA_GPIO GPIO_NUM_41
#define IMU_SCL_GPIO GPIO_NUM_42
#define IMU_ADDRESS  0x6B
#define RAD_TO_DEG   (180.0f / 3.14159265358979323846f)

static const char *TAG = "imu_demo";

static float angle_delta(float target, float current)
{
    float delta = target - current;
    while (delta > 180.0f) delta -= 360.0f;
    while (delta < -180.0f) delta += 360.0f;
    return delta;
}

void app_main(void)
{
    vibration_start();
    led_demo_start();
    max30102_demo_start();
    telemetry_start();
    nrf24_demo_start();
    input_voltage_start();
    camera_uplink_start();

    i2c_master_bus_handle_t bus = NULL;
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = IMU_SDA_GPIO,
        .scl_io_num = IMU_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));

    qmi8658_dev_t imu = {0};
    esp_err_t err = qmi8658_init(&imu, bus, IMU_ADDRESS);
    if (err != ESP_OK) {
        telemetry_set_imu_calibration(GYRO_CAL_ERROR, 0, NULL);
        ESP_LOGE(TAG, "QMI8658A not found at 0x%02X (SDA=41, SCL=42): %s",
                 IMU_ADDRESS, esp_err_to_name(err));
        return;
    }

    // Waveshare's driver targets QMI8658C. Apply QMI8658A Rev A settings:
    // auto-increment + little-endian, accel +/-4 g, gyro +/-512 dps,
    // both at 112.1 Hz in 6DOF mode. The A gyro range codes differ from C.
    ESP_ERROR_CHECK(qmi8658_enable_sensors(&imu, QMI8658_DISABLE_ALL));
    ESP_ERROR_CHECK(qmi8658_write_register(&imu, QMI8658_CTRL1, 0x40));
    ESP_ERROR_CHECK(qmi8658_set_accel_range(&imu, QMI8658_ACCEL_RANGE_4G));
    ESP_ERROR_CHECK(qmi8658_set_accel_odr(&imu, QMI8658_ACCEL_ODR_125HZ));
    ESP_ERROR_CHECK(qmi8658_write_register(&imu, QMI8658_CTRL3, 0x56));
    imu.gyro_lsb_div = 64;  // +/-512 dps on QMI8658A = 64 LSB/(deg/s)
    qmi8658_set_accel_unit_mg(&imu, true);
    qmi8658_set_gyro_unit_dps(&imu, true);
    ESP_ERROR_CHECK(qmi8658_enable_sensors(&imu, QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO));
    vTaskDelay(pdMS_TO_TICKS(250));  // Allow the gyroscope to settle.

    ESP_LOGI(TAG, "Gyro startup calibration: hold steady for 3 seconds; gyro span <=5 dps per axis");
    gyro_calibration_t calibration = {0};
    float roll = 0.0f;
    float pitch = 0.0f;
    int64_t last_sample_us = 0;
    int64_t last_print_us = 0;
    int64_t last_reset_print_us = 0;

    while (true) {
        uint8_t status = 0;
        err = qmi8658_read_register(&imu, QMI8658_STATUS0, &status, 1);
        if (err == ESP_OK && (status & 0x03) == 0x03) {
            qmi8658_data_t sample = {0};
            err = qmi8658_read_sensor_data(&imu, &sample);
            if (err == ESP_OK && (!isfinite(sample.accelX) || !isfinite(sample.accelY) ||
                !isfinite(sample.accelZ) || !isfinite(sample.gyroX) ||
                !isfinite(sample.gyroY) || !isfinite(sample.gyroZ)))
                err = ESP_ERR_INVALID_RESPONSE;
            if (err == ESP_OK) {
                const int64_t now_us = esp_timer_get_time();
                if (calibration.state != GYRO_CAL_READY) {
                    const float accel[3] = {sample.accelX, sample.accelY, sample.accelZ};
                    const float gyro[3] = {sample.gyroX, sample.gyroY, sample.gyroZ};
                    const unsigned previous_count = calibration.count;
                    const uint32_t gap_ms = (uint32_t)(now_us / 1000) - calibration.last_ms;
                    float accel_span = 0, gyro_span = 0;
                    if (previous_count) for (unsigned i = 0; i < 3; ++i) {
                        accel_span = fmaxf(accel_span, fmaxf(calibration.accel_max[i], accel[i]) -
                                                     fminf(calibration.accel_min[i], accel[i]));
                        gyro_span = fmaxf(gyro_span, fmaxf(calibration.gyro_max[i], gyro[i]) -
                                                   fminf(calibration.gyro_min[i], gyro[i]));
                    }
                    const bool ready = gyro_calibration_update(&calibration, accel, gyro,
                                                               (uint32_t)(now_us / 1000));
                    const unsigned progress = gyro_calibration_progress(&calibration);
                    telemetry_set_imu_calibration(calibration.state, progress, calibration.bias);
                    if (previous_count && calibration.count <= 1 && now_us-last_reset_print_us >= 1000000) {
                        ESP_LOGI(TAG, "Gyro window restarted state=%u gap=%lu ms span A/G=%.2f/%.2f",
                                 calibration.state, (unsigned long)gap_ms, accel_span, gyro_span);
                        last_reset_print_us = now_us;
                    }
                    if (!ready) {
                        if (now_us - last_print_us >= 1000000) {
                            ESP_LOGI(TAG, "Gyro calibration state=%u progress=%u%% prev=%u gap=%lu ms span A/G=%.2f/%.2f mg/dps G=%.2f/%.2f/%.2f",
                                     calibration.state, progress, previous_count, (unsigned long)gap_ms,
                                     accel_span, gyro_span, gyro[0], gyro[1], gyro[2]);
                            last_print_us = now_us;
                        }
                        vTaskDelay(pdMS_TO_TICKS(10));
                        continue; // No usable attitude / launch until startup calibration completes.
                    }
                    ESP_LOGI(TAG, "Gyro calibrated: bias XYZ=%.4f %.4f %.4f dps, %u samples / %lu ms",
                             calibration.bias[0], calibration.bias[1], calibration.bias[2],
                             calibration.count, (unsigned long)(calibration.last_ms-calibration.first_ms));
                    // Use the same stationary window to establish the initial flat reference.
                    sample.accelX = calibration.accel_mean[0];
                    sample.accelY = calibration.accel_mean[1];
                    sample.accelZ = calibration.accel_mean[2];
                }
                sample.gyroX -= calibration.bias[0];
                sample.gyroY -= calibration.bias[1];
                sample.gyroZ -= calibration.bias[2];
                const float accel_roll = atan2f(sample.accelY, sample.accelZ) * RAD_TO_DEG;
                const float accel_pitch = atan2f(-sample.accelX,
                                                 hypotf(sample.accelY, sample.accelZ)) * RAD_TO_DEG;
                if (last_sample_us == 0) {
                    roll = accel_roll;
                    pitch = accel_pitch;
                } else {
                    float dt = (now_us - last_sample_us) / 1000000.0f;
                    if (dt > 0.05f) dt = 0.05f;
                    const float predicted_roll = roll + sample.gyroX * dt;
                    const float predicted_pitch = pitch + sample.gyroY * dt;
                    // Complementary filter: gyro tracks motion, accel corrects drift.
                    roll = predicted_roll + 0.02f * angle_delta(accel_roll, predicted_roll);
                    pitch = predicted_pitch + 0.02f * (accel_pitch - predicted_pitch);
                    if (roll > 180.0f) roll -= 360.0f;
                    if (roll < -180.0f) roll += 360.0f;
                }
                last_sample_us = now_us;
                telemetry_set_attitude(pitch, roll);

                if (now_us - last_print_us >= 100000) {
                    ESP_LOGI(TAG, "pitch=%+7.1f deg  roll=%+7.1f deg", pitch, roll);
                    last_print_us = now_us;
                }
            }
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "IMU read failed: %s", esp_err_to_name(err));
            if (calibration.state != GYRO_CAL_READY) {
                gyro_calibration_restart(&calibration, GYRO_CAL_ERROR);
                telemetry_set_imu_calibration(GYRO_CAL_ERROR, 0, NULL);
            }
            last_sample_us = 0;
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            if (calibration.state != GYRO_CAL_READY && calibration.count &&
                (uint32_t)((uint32_t)(esp_timer_get_time()/1000)-calibration.last_ms)>250) {
                gyro_calibration_restart(&calibration, GYRO_CAL_ERROR);
                telemetry_set_imu_calibration(GYRO_CAL_ERROR, 0, NULL);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
