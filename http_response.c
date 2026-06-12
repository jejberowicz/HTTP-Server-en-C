#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "http_response.h"

const char *http_status_phrase(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        default:  return "Unknown";
    }
}

void http_response_init(http_response_t *res, int status_code) {
    memset(res, 0, sizeof(*res));
    res->status_code = status_code;
}

void http_response_add_header(http_response_t *res,
                               const char *name, const char *value) {
    if (res->header_count >= MAX_RESP_HEADERS)
        return;
    snprintf(res->headers[res->header_count], MAX_RESP_HDR_LINE,
             "%s: %s", name, value);
    res->header_count++;
}

int http_response_write(const http_response_t *res, char *buf, int cap) {
    int written = 0;

#define APPEND(fmt, ...) \
    do { \
        int n = snprintf(buf + written, cap - written, fmt, ##__VA_ARGS__); \
        if (n < 0 || n >= cap - written) return -1; \
        written += n; \
    } while (0)

    APPEND("HTTP/1.1 %d %s\r\n",
           res->status_code, http_status_phrase(res->status_code));

    for (int i = 0; i < res->header_count; i++)
        APPEND("%s\r\n", res->headers[i]);

    /* Content-Length from body */
    if (res->body && res->body_len > 0)
        APPEND("Content-Length: %d\r\n", res->body_len);
    else
        APPEND("%s", "Content-Length: 0\r\n");

    APPEND("%s", "\r\n");

    if (res->body && res->body_len > 0) {
        if (written + res->body_len >= cap)
            return -1;
        memcpy(buf + written, res->body, res->body_len);
        written += res->body_len;
    }

#undef APPEND

    return written;
}

int http_response_send(const http_response_t *res, int fd) {
    char buf[65536];
    int len = http_response_write(res, buf, sizeof(buf));
    if (len < 0)
        return -1;

    int sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n <= 0)
            return -1;
        sent += (int)n;
    }
    return sent;
}
