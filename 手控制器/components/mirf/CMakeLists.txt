idf_component_register(SRCS "mirf.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_spi
                       PRIV_REQUIRES esp_driver_gpio)
