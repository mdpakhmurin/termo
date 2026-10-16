#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "setup_page.h"
#include "wifi.h"
#include "web_utils.h"

// HTML-страница из файла setup_page.html
extern const char PAGE[] asm("_binary_setup_page_html_start");

#define MAX_NETWORKS 15

static esp_err_t on_get_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, PAGE);
}

static esp_err_t on_get_state(httpd_req_t *req)
{
    static char json[4096];
    static wifi_network_t networks[MAX_NETWORKS];

    char saved_ssid[33] = "";
    char saved_password[64];
    wifi_load_credentials(saved_ssid, sizeof(saved_ssid), saved_password, sizeof(saved_password));

    char safe_ssid[200];
    json_escape(saved_ssid, safe_ssid, sizeof(safe_ssid));

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

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t on_save(httpd_req_t *req)
{
    char body[400];
    if (req->content_len >= sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Too long");
    }
    int length = httpd_req_recv(req, body, req->content_len);
    if (length <= 0) {
        return ESP_FAIL;
    }
    body[length] = '\0';

    char ssid[100] = "";
    char password[200] = "";
    httpd_query_key_value(body, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(body, "password", password, sizeof(password));
    url_decode(ssid);
    url_decode(password);

    printf("Получили сеть: %s (пароль: %d символов)\n", ssid, (int)strlen(password));

    httpd_resp_set_type(req, "text/html; charset=utf-8");

    size_t ssid_length = strlen(ssid);
    size_t password_length = strlen(password);
    if (ssid_length == 0 || ssid_length > 32) {
        return httpd_resp_sendstr(req, "<meta charset='utf-8'><h2>Имя сети: от 1 до 32 символов</h2>");
    }
    if (password_length < 8 || password_length > 63) {
        return httpd_resp_sendstr(req, "<meta charset='utf-8'><h2>Пароль: от 8 до 63 символов</h2>");
    }

    if (!wifi_save_credentials(ssid, password)) {
        printf("Не удалось сохранить\n");
        return httpd_resp_sendstr(req, "<meta charset='utf-8'><h2>Ошибка сохранения</h2>");
    }

    printf("Сеть сохранена\n");

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t on_finish(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    char ssid[33];
    char password[64];
    if (!wifi_load_credentials(ssid, sizeof(ssid), password, sizeof(password))) {
        return httpd_resp_sendstr(req, "<meta charset='utf-8'><h2>Сначала сохраните сеть</h2>");
    }

    printf("Настройка завершена, перезагружаюсь\n");
    httpd_resp_sendstr(req, "<meta charset='utf-8'><h2>Настройка завершена. Устройство будет перезагружено</h2>");

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

void setup_page_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    httpd_uri_t page = { .uri = "/", .method = HTTP_GET, .handler = on_get_page };
    httpd_uri_t state = { .uri = "/api/state", .method = HTTP_GET, .handler = on_get_state };
    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = on_save };
    httpd_uri_t finish = { .uri = "/finish", .method = HTTP_POST, .handler = on_finish };
    httpd_register_uri_handler(server, &page);
    httpd_register_uri_handler(server, &state);
    httpd_register_uri_handler(server, &save);
    httpd_register_uri_handler(server, &finish);

    printf("Страница настройки: http://192.168.4.1\n");
}
