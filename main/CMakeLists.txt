idf_component_register(SRCS "wifi.c" "mqtt.c" "bme280.c" "main.c"
                       PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_wifi esp_netif nvs_flash
                       INCLUDE_DIRS "")
