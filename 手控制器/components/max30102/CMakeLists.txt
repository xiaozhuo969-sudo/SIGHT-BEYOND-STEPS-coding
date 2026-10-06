idf_component_register(SRCS "esp_max30102.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_i2c esp_driver_gpio freertos log)
