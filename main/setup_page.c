#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "setup_page.h"
#include "wifi.h"

// HTML-страница из файла setup_page.html
extern const char PAGE[] asm("_binary_setup_page_html_start");

static void url_decode(char *text)
{
    char *out = text;
    for (char *in = text; *in != '\0'; in++) {
        if (*in == '+') {
            *out++ = ' ';
        } else if (*in == '%' && isxdigit((unsigned char)in[1]) && isxdigit((unsigned char)in[2])) {
            char hex[3] = { in[1], in[2], '\0' };
            *out++ = (char)strtol(hex, NULL, 16);
            in += 2;
        } else {
            *out++ = *in;
        }
    }
    *out = '\0';
}

static esp_err_t on_get_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, PAGE);
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
