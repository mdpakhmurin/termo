#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_http_server.h"

// Отправить содержимое с MIME-типом, например web_send(req, "text/css", STYLE_CSS)
esp_err_t web_send(httpd_req_t *req, const char *content_type, const char *content);

// Короткая страница-ответ
esp_err_t web_send_message(httpd_req_t *req, const char *text);

// Редирект
esp_err_t web_redirect(httpd_req_t *req, const char *location);

// Прочитать тело формы в body
bool web_read_form(httpd_req_t *req, char *body, size_t size);

// Раскодировать текст из формата url
void url_decode(char *text);

// Заменить опасные JSON символы
void json_escape(const char *in, char *out, size_t size);

// Заменить опасные HTML символы
void html_escape(const char *in, char *out, size_t size);
