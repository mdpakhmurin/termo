#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "setup_page.h"
#include "wifi.h"
#include "mqtt.h"
#include "web_utils.h"

extern const char SETUP_PAGE[] asm("_binary_setup_page_html_start");
extern const char MQTT_PAGE[]  asm("_binary_mqtt_page_html_start");
extern const char STYLE_CSS[]  asm("_binary_style_css_start");

#define MAX_NETWORKS 15

static esp_err_t on_get_setup_page(httpd_req_t *req)
{
    return web_send(req, "text/html; charset=utf-8", SETUP_PAGE);
}

static esp_err_t on_get_mqtt_page(httpd_req_t *req)
{
    return web_send(req, "text/html; charset=utf-8", MQTT_PAGE);
}

static esp_err_t on_get_style(httpd_req_t *req)
{
    return web_send(req, "text/css; charset=utf-8", STYLE_CSS);
}

static esp_err_t on_get_wifi(httpd_req_t *req)
{
    static char json[4096];
    static wifi_network_t networks[MAX_NETWORKS];

    wifi_credentials_t saved;
    if (!wifi_load_credentials(&saved)) {
        saved.ssid[0] = '\0';
    }

    char safe_ssid[200];
    json_escape(saved.ssid, safe_ssid, sizeof(safe_ssid));

    int pos = snprintf(json, sizeof(json), "{\"saved_ssid\":\"%s\",\"networks\":[", safe_ssid);

    int count = wifi_scan(networks, MAX_NETWORKS);
    for (int i = 0; i < count; i++) {
        json_escape(networks[i].ssid, safe_ssid, sizeof(safe_ssid));
        int written = snprintf(json + pos, sizeof(json) - pos,
                               "%s{\"ssid\":\"%s\",\"rssi\":%d,\"secured\":%s}",
                               i > 0 ? "," : "",
                               safe_ssid,
                               networks[i].rssi,
                               networks[i].secured ? "true" : "false");
        if (written < 0 || pos + written >= (int)sizeof(json) - 3) {
            break;
        }
        pos += written;
    }

    snprintf(json + pos, sizeof(json) - pos, "]}");

    return web_send(req, "application/json", json);
}

static esp_err_t on_save_wifi(httpd_req_t *req)
{
    char body[400];
    if (!web_read_form(req, body, sizeof(body))) {
        return ESP_FAIL;
    }

    char ssid[100] = "";
    char password[200] = "";
    httpd_query_key_value(body, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(body, "password", password, sizeof(password));
    url_decode(ssid);
    url_decode(password);

    printf("Получили сеть: %s (пароль: %d символов)\n", ssid, (int)strlen(password));

    size_t ssid_length = strlen(ssid);
    size_t password_length = strlen(password);
    if (ssid_length == 0 || ssid_length >= WIFI_SSID_SIZE) {
        return web_send_error(req, "Имя сети: от 1 до 32 символов");
    }
    if (password_length < 8 || password_length >= WIFI_PASSWORD_SIZE) {
        return web_send_error(req, "Пароль: от 8 до 63 символов");
    }

    wifi_credentials_t credentials;
    strlcpy(credentials.ssid, ssid, sizeof(credentials.ssid));
    strlcpy(credentials.password, password, sizeof(credentials.password));

    if (!wifi_save_credentials(&credentials)) {
        printf("Не удалось сохранить сеть\n");
        return web_send_error(req, "Ошибка сохранения");
    }

    printf("Сеть сохранена\n");
    return web_send_ok(req);
}

static esp_err_t on_get_mqtt(httpd_req_t *req)
{
    mqtt_settings_t saved;
    if (!mqtt_load_settings(&saved)) {
        saved.uri[0] = '\0';
        saved.username[0] = '\0';
    }

    static char safe_uri[MQTT_URI_SIZE * 6];
    static char safe_username[MQTT_USERNAME_SIZE * 6];
    static char json[MQTT_URI_SIZE * 6 + MQTT_USERNAME_SIZE * 6 + 64];
    json_escape(saved.uri, safe_uri, sizeof(safe_uri));
    json_escape(saved.username, safe_username, sizeof(safe_username));
    snprintf(json, sizeof(json), "{\"uri\":\"%s\",\"username\":\"%s\"}", safe_uri, safe_username);

    return web_send(req, "application/json", json);
}

static esp_err_t on_save_mqtt(httpd_req_t *req)
{
    static char body[1000];
    if (!web_read_form(req, body, sizeof(body))) {
        return ESP_FAIL;
    }

    char uri[400] = "";
    char username[200] = "";
    char password[200] = "";
    httpd_query_key_value(body, "uri", uri, sizeof(uri));
    httpd_query_key_value(body, "username", username, sizeof(username));
    httpd_query_key_value(body, "password", password, sizeof(password));
    url_decode(uri);
    url_decode(username);
    url_decode(password);

    printf("Получили брокер: %s, логин %s (пароль: %d символов)\n", uri, username, (int)strlen(password));

    size_t uri_length = strlen(uri);
    if (uri_length >= MQTT_URI_SIZE ||
        (strncmp(uri, "mqtt://", 7) != 0 && strncmp(uri, "mqtts://", 8) != 0)) {
        return web_send_error(req, "Адрес должен начинаться с mqtt:// или mqtts:// (до 127 символов)");
    }
    if (strlen(username) >= MQTT_USERNAME_SIZE) {
        return web_send_error(req, "Логин: до 63 символов");
    }
    size_t password_length = strlen(password);
    if (password_length == 0 || password_length >= MQTT_PASSWORD_SIZE) {
        return web_send_error(req, "Пароль: от 1 до 63 символов");
    }

    mqtt_settings_t settings;
    strlcpy(settings.uri, uri, sizeof(settings.uri));
    strlcpy(settings.username, username, sizeof(settings.username));
    strlcpy(settings.password, password, sizeof(settings.password));

    if (!mqtt_save_settings(&settings)) {
        printf("Не удалось сохранить брокер\n");
        return web_send_error(req, "Ошибка сохранения");
    }

    printf("Брокер сохранён\n");
    return web_send_ok(req);
}

static esp_err_t on_get_status(httpd_req_t *req)
{
    wifi_credentials_t wifi;
    mqtt_settings_t mqtt;
    bool wifi_saved = wifi_load_credentials(&wifi);
    bool mqtt_saved = mqtt_load_settings(&mqtt);

    char json[64];
    snprintf(json, sizeof(json), "{\"wifi_saved\":%s,\"mqtt_saved\":%s}",
             wifi_saved ? "true" : "false",
             mqtt_saved ? "true" : "false");

    return web_send(req, "application/json", json);
}

static esp_err_t on_finish(httpd_req_t *req)
{
    wifi_credentials_t wifi;
    if (!wifi_load_credentials(&wifi)) {
        return web_send_message(req, "Сначала сохраните сеть Wi-Fi");
    }

    mqtt_settings_t mqtt;
    if (!mqtt_load_settings(&mqtt)) {
        return web_send_message(req, "Сначала сохраните настройки MQTT");
    }

    printf("Настройка завершена, перезагружаюсь\n");
    web_send_message(req, "Настройка завершена. Устройство будет перезагружено");

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

void setup_page_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.stack_size = 6144;
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    httpd_uri_t handlers[] = {
        { .uri = "/",          .method = HTTP_GET,  .handler = on_get_setup_page },
        { .uri = "/mqtt",      .method = HTTP_GET,  .handler = on_get_mqtt_page },
        { .uri = "/style.css", .method = HTTP_GET,  .handler = on_get_style },
        { .uri = "/api/wifi",  .method = HTTP_GET,  .handler = on_get_wifi },
        { .uri = "/api/mqtt",  .method = HTTP_GET,  .handler = on_get_mqtt },
        { .uri = "/api/status", .method = HTTP_GET, .handler = on_get_status },
        { .uri = "/save_wifi", .method = HTTP_POST, .handler = on_save_wifi },
        { .uri = "/save_mqtt", .method = HTTP_POST, .handler = on_save_mqtt },
        { .uri = "/finish",    .method = HTTP_POST, .handler = on_finish },
    };
    int handler_count = sizeof(handlers) / sizeof(handlers[0]);
    for (int i = 0; i < handler_count; i++) {
        httpd_register_uri_handler(server, &handlers[i]);
    }

    printf("Страница настройки: http://192.168.4.1\n");
}
