idf_component_register(
    SRCS "main.c" "mpu6050.c" "wifi_sta.c" "web_server.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_http_server esp_netif nvs_flash esp_driver_i2c esp_event esp_timer
)
