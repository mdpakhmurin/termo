#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "wifi.h"
#include "wifi_secrets.h"

// имя раздаваемой сети
#define SETUP_AP_SSID "Termo-Setup"

// Пауза перед повторным подключением
#define RECONNECT_DELAY_MIN_MS  (1 * 1000)
#define RECONNECT_DELAY_STEP_MS (1 * 1000)
#define RECONNECT_DELAY_MAX_MS  (20 * 1000)

// Время неудачного подключения, после которого плата будет перезагружена
#define GIVE_UP_TIMEOUT_MS (20 * 60 * 1000)

static volatile bool connected = false;

static esp_timer_handle_t reconnect_timer = NULL;
static int reconnect_delay_ms = RECONNECT_DELAY_MIN_MS;

static esp_timer_handle_t give_up_timer = NULL;

bool wifi_is_setup_mode(void)
{
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK) {
        return false;
    }
    return mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA;
}

static void on_reconnect_timer(void *arg)
{
    esp_wifi_connect();
}

static void on_give_up_timer(void *arg)
{
    printf("Не можем подключиться %d с — перезагружаюсь\n", GIVE_UP_TIMEOUT_MS / 1000);
    esp_restart();
}

static void start_give_up_timer(void)
{
    if (!esp_timer_is_active(give_up_timer)) {
        esp_timer_start_once(give_up_timer, (uint64_t)GIVE_UP_TIMEOUT_MS * 1000);
    }
}

static void on_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START && !wifi_is_setup_mode()) {
        printf("Wi-Fi запустился, подключаюсь...\n");
        esp_wifi_connect();
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED && !wifi_is_setup_mode()) {
        printf("Отключились от роутера, повтор через %d с\n", reconnect_delay_ms / 1000);
        connected = false;
        start_give_up_timer();

        esp_timer_start_once(reconnect_timer, (uint64_t)reconnect_delay_ms * 1000);   // в микросекундах

        reconnect_delay_ms += RECONNECT_DELAY_STEP_MS;
        if (reconnect_delay_ms > RECONNECT_DELAY_MAX_MS) {
            reconnect_delay_ms = RECONNECT_DELAY_MAX_MS;
        }
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        printf("Получили IP!\n");
        connected = true;
        reconnect_delay_ms = RECONNECT_DELAY_MIN_MS;
        esp_timer_stop(give_up_timer);
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

bool wifi_save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READWRITE, &nvs) != ESP_OK) {
        return false;
    }

    esp_err_t err = nvs_set_str(nvs, "ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "password", password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    return err == ESP_OK;
}

bool wifi_load_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }

    esp_err_t err = nvs_get_str(nvs, "ssid", ssid, &ssid_size);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, "password", password, &password_size);
    }
    nvs_close(nvs);

    return err == ESP_OK;
}

void wifi_clear_credentials(void)
{
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_erase_all(nvs);
    nvs_commit(nvs);
    nvs_close(nvs);
}

void wifi_request_setup_mode(void)
{
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_set_u8(nvs, "setup", 1);
    nvs_commit(nvs);
    nvs_close(nvs);
}

bool wifi_take_setup_request(void)
{
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READWRITE, &nvs) != ESP_OK) {
        return false;
    }

    uint8_t requested = 0;
    nvs_get_u8(nvs, "setup", &requested);
    if (requested) {
        nvs_erase_key(nvs, "setup");
        nvs_commit(nvs);
    }
    nvs_close(nvs);

    return requested != 0;
}

void wifi_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));

    esp_timer_create_args_t timer_args = {
        .callback = on_reconnect_timer,
        .name = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &reconnect_timer));

    esp_timer_create_args_t give_up_args = {
        .callback = on_give_up_timer,
        .name = "wifi_give_up",
    };
    ESP_ERROR_CHECK(esp_timer_create(&give_up_args, &give_up_timer));

    printf("Wi-Fi иницализрован\n");
}

void wifi_start_access_point(void)
{
    printf("Раздаю сеть %s\n", SETUP_AP_SSID);
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

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
    printf("Подключаюсь к сети %s\n", ssid);
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());
}

static int by_signal(const void *a, const void *b)
{
    return ((const wifi_ap_record_t *)b)->rssi - ((const wifi_ap_record_t *)a)->rssi;
}

static bool already_listed(const wifi_network_t *networks, int count, const char *ssid)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(networks[i].ssid, ssid) == 0) {
            return true;
        }
    }
    return false;
}

int wifi_scan(wifi_network_t *networks, int max_networks)
{
    wifi_scan_config_t scan_config = {0};
    if (esp_wifi_scan_start(&scan_config, true) != ESP_OK) {
        printf("Не удалось просканировать сети\n");
        return 0;
    }

    static wifi_ap_record_t records[20];
    uint16_t count = 20;
    esp_wifi_scan_get_ap_records(&count, records);
    qsort(records, count, sizeof(records[0]), by_signal);

    int found = 0;
    for (int i = 0; i < count && found < max_networks; i++) {
        const char *ssid = (const char *)records[i].ssid;
        if (ssid[0] == '\0' || already_listed(networks, found, ssid)) {
            continue;
        }
        strlcpy(networks[found].ssid, ssid, sizeof(networks[found].ssid));
        networks[found].rssi = records[i].rssi;
        networks[found].secured = records[i].authmode != WIFI_AUTH_OPEN;
        found++;
    }

    printf("Нашли сетей: %d\n", found);
    return found;
}
