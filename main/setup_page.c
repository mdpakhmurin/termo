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

// Метка в HTML, на место которой вставляем найденные сети
#define NETWORKS_MARKER "<!--NETWORKS-->"

#define MAX_NETWORKS 15

static const char *signal_bars(int rssi)
{
    if (rssi > -55) return "▂▄▆█";
    if (rssi > -65) return "▂▄▆";
    if (rssi > -75) return "▂▄";
    return "▂";
}

static void send_network_options(httpd_req_t *req)
{
    static wifi_network_t networks[MAX_NETWORKS];
    int count = wifi_scan(networks, MAX_NETWORKS);

    for (int i = 0; i < count; i++) {
        char safe_name[200];
        html_escape(networks[i].ssid, safe_name, sizeof(safe_name));

        char option[512];
        snprintf(option, sizeof(option), "<option value=\"%s\">%s  %s%s</option>",
                 safe_name, safe_name, signal_bars(networks[i].rssi), networks[i].secured ? " 🔒" : "");
        httpd_resp_sendstr_chunk(req, option);
    }
}

static esp_err_t on_get_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    const char *marker = strstr(PAGE, NETWORKS_MARKER);
    if (marker == NULL) {
        return httpd_resp_sendstr(req, PAGE);
    }

    httpd_resp_send_chunk(req, PAGE, marker - PAGE);                  // всё до метки
    send_network_options(req);                                        // список сетей
    httpd_resp_sendstr_chunk(req, marker + strlen(NETWORKS_MARKER));  // всё после метки
    return httpd_resp_sendstr_chunk(req, NULL);                       // NULL — ответ закончен
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
    if (password_length != 0 && (password_length < 8 || password_length > 63)) {
        return httpd_resp_sendstr(req, "<meta charset='utf-8'><h2>Пароль: от 8 до 63 символов</h2>");
    }

    if (!wifi_save_credentials(ssid, password)) {
        printf("Не удалось сохранить\n");
        return httpd_resp_sendstr(req, "<meta charset='utf-8'><h2>Ошибка сохранения</h2>");
    }

    printf("Сохранено, перезагружаюсь\n");
    httpd_resp_sendstr(req, "<meta charset='utf-8'><h2>Сохранено! Устройство будет перезагружено в режиме подключения</h2>");

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
    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = on_save };
    httpd_register_uri_handler(server, &page);
    httpd_register_uri_handler(server, &save);

    printf("Страница настройки: http://192.168.4.1\n");
}
