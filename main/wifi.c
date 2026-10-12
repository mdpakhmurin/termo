#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "wifi_secrets.h"

// имя раздаваемой сети
#define SETUP_AP_SSID "Termo-Setup"

static volatile bool connected = false;

static void on_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        printf("Wi-Fi запустился, подключаюсь...\n");
        esp_wifi_connect();
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        printf("Отключились от роутера, подключаюсь снова...\n");
        connected = false;
        esp_wifi_connect();
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        printf("Получили IP!\n");
        connected = true;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        printf("Входящее подключение %s\n", SETUP_AP_SSID);
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        printf("Входящее подключение разорвано %s\n", SETUP_AP_SSID);
    }
}

bool wifi_is_connected(void)
{
    return connected;
}

static void setup_wifi_events(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));

    printf("Обработчик событий Wi-Fi подключён\n");
}

static void setup_network(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();   // wifi - клиент
    esp_netif_create_default_wifi_ap();    // wifi - точка доступ

    printf("Сетевой стек готов\n");
}

static void start_wifi(const char *ssid, const char *password)
{
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = SETUP_AP_SSID,
            .password = SETUP_AP_PASSWORD, // Минимум 8 символов
            .channel = 1,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = 2,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_connect(const char *ssid, const char *password)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    setup_wifi_events();
    setup_network();
    start_wifi(ssid, password);
}
