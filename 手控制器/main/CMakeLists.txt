idf_component_register(SRCS "main.c" "led_demo.c" "max30102_demo.c"
                            "telemetry.c" "vibration.c" "nrf24_demo.c" "gesture.c" "remote_protocol.c" "control_output.c" "tx_control.c"
                            "input_voltage.c" "camera_uplink.c" "flight_keys.c" "gyro_calibration.c" "../../shared/hud_protocol.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_ledc
                                esp_driver_usb_serial_jtag esp_timer max30102 esp_health mirf
                                esp_adc esp_wifi esp_netif esp_event nvs_flash tcp_transport espressif__cjson)
