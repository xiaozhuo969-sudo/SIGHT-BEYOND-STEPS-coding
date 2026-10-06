option(CAMERA_HUD_SELFTEST "Run read-only HTTPD WebSocket loopback check using live controller data" OFF)
idf_component_register(SRCS "main.c" "network.c" "camera_server.c" "hud_server.c" "../../shared/hud_protocol.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_http_server nvs_flash esp_psram
                  esp_driver_gpio esp_timer spi_flash espressif__cjson tcp_transport
    EMBED_TXTFILES "web/index.html")
if(CAMERA_HUD_SELFTEST)
    target_sources(${COMPONENT_LIB} PRIVATE "../tests/hud_server_selftest.c")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CAMERA_HUD_SELFTEST=1)
endif()
