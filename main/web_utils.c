#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "web_utils.h"

esp_err_t web_send(httpd_req_t *req, const char *content_type, const char *content)
{
    httpd_resp_set_type(req, content_type);
    return httpd_resp_sendstr(req, content);
}

esp_err_t web_send_message(httpd_req_t *req, const char *text)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, "<meta charset='utf-8'><h2>");
    httpd_resp_sendstr_chunk(req, text);
    httpd_resp_sendstr_chunk(req, "</h2>");
    return httpd_resp_sendstr_chunk(req, NULL);
}

esp_err_t web_redirect(httpd_req_t *req, const char *location)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", location);
    return httpd_resp_send(req, NULL, 0);
}

bool web_read_form(httpd_req_t *req, char *body, size_t size)
{
    if (req->content_len >= size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Too long");
        return false;
    }
    int length = httpd_req_recv(req, body, req->content_len);
    if (length <= 0) {
        return false;
    }
    body[length] = '\0';
    return true;
}

void url_decode(char *text)
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

void json_escape(const char *in, char *out, size_t size)
{
    size_t pos = 0;
    for (; *in != '\0'; in++) {
        unsigned char c = (unsigned char)*in;
        char piece[8];
        if (c == '"' || c == '\\') {
            piece[0] = '\\';
            piece[1] = (char)c;
            piece[2] = '\0';
        } else if (c < 0x20) {
            snprintf(piece, sizeof(piece), "\\u%04x", c);
        } else {
            piece[0] = (char)c;
            piece[1] = '\0';
        }

        size_t length = strlen(piece);
        if (pos + length >= size) {
            break;
        }
        memcpy(out + pos, piece, length);
        pos += length;
    }
    out[pos] = '\0';
}

void html_escape(const char *in, char *out, size_t size)
{
    size_t pos = 0;
    for (; *in != '\0'; in++) {
        const char *replacement = NULL;
        switch (*in) {
            case '&':  replacement = "&amp;";  break;
            case '<':  replacement = "&lt;";   break;
            case '>':  replacement = "&gt;";   break;
            case '\'': replacement = "&#39;";  break;
            case '"':  replacement = "&quot;"; break;
        }
        size_t length = replacement ? strlen(replacement) : 1;
        if (pos + length >= size) {
            break;
        }
        if (replacement) {
            memcpy(out + pos, replacement, length);
        } else {
            out[pos] = *in;
        }
        pos += length;
    }
    out[pos] = '\0';
}
