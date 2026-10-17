idf_component_register(SRCS "wifi.c" "mqtt.c" "bme280.c" "setup_page.c" "web_utils.c" "main.c"
                       PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_wifi esp_netif nvs_flash esp_http_server esp_timer
                       INCLUDE_DIRS ""
                       EMBED_TXTFILES "setup_page.html" "mqtt_page.html" "style.css")
