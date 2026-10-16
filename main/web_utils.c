#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "web_utils.h"

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
