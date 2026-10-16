#pragma once

#include <stddef.h>

// Раскодировать текст из формата url
void url_decode(char *text);

// Заменить опасные JSON символы
void json_escape(const char *in, char *out, size_t size);

// Заменить опасные HTML символы
void html_escape(const char *in, char *out, size_t size);
