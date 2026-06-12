#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "http_request.h"

/* Lowercase a string in-place. */
static void str_tolower(char *s) {
    for (; *s; s++)
        *s = (char)tolower((unsigned char)*s);
}

/*
 * Find the first occurrence of `needle` (length `nlen`) in `haystack`
 * (length `hlen`).  Returns a pointer to the match or NULL.
 */
static const char *memmem_simple(const char *haystack, int hlen,
                                  const char *needle,   int nlen) {
    if (nlen == 0) return haystack;
    for (int i = 0; i <= hlen - nlen; i++) {
        if (memcmp(haystack + i, needle, nlen) == 0)
            return haystack + i;
    }
    return NULL;
}

parse_result_t http_request_parse(const char *buf, int len, http_request_t *req) {
    if (!buf || len <= 0 || !req)
        return PARSE_ERROR;

    memset(req, 0, sizeof(*req));

    /* ---- 1. Locate end of headers ---- */
    const char *header_end = memmem_simple(buf, len, "\r\n\r\n", 4);
    if (!header_end)
        return PARSE_INCOMPLETE;

    /* ---- 2. Parse request line ---- */
    const char *line_end = memmem_simple(buf, len, "\r\n", 2);
    if (!line_end)
        return PARSE_INCOMPLETE;

    /* method */
    const char *p = buf;
    const char *space = memchr(p, ' ', line_end - p);
    if (!space)
        return PARSE_ERROR;

    int method_len = (int)(space - p);
    if (method_len >= MAX_METHOD_LEN)
        return PARSE_ERROR;
    memcpy(req->method, p, method_len);
    req->method[method_len] = '\0';
    p = space + 1;

    /* path */
    space = memchr(p, ' ', line_end - p);
    if (!space)
        return PARSE_ERROR;

    int path_len = (int)(space - p);
    if (path_len >= MAX_PATH_LEN)
        return PARSE_ERROR;
    memcpy(req->path, p, path_len);
    req->path[path_len] = '\0';
    p = space + 1;

    /* version */
    int version_len = (int)(line_end - p);
    if (version_len <= 0 || version_len >= MAX_VERSION_LEN)
        return PARSE_ERROR;
    memcpy(req->version, p, version_len);
    req->version[version_len] = '\0';

    /* ---- 3. Parse headers ---- */
    p = line_end + 2;  /* skip first \r\n */

    while (p < header_end) {
        const char *eol = memmem_simple(p, (int)(header_end - p), "\r\n", 2);
        if (!eol)
            break;

        if (req->header_count >= MAX_HEADERS)
            return PARSE_ERROR;

        /* name */
        const char *colon = memchr(p, ':', eol - p);
        if (!colon)
            return PARSE_ERROR;

        int name_len = (int)(colon - p);
        if (name_len <= 0 || name_len >= MAX_HEADER_NAME)
            return PARSE_ERROR;

        http_header_t *h = &req->headers[req->header_count];
        memcpy(h->name, p, name_len);
        h->name[name_len] = '\0';
        str_tolower(h->name);

        /* value: skip leading whitespace after colon */
        const char *val = colon + 1;
        while (val < eol && (*val == ' ' || *val == '\t'))
            val++;

        int val_len = (int)(eol - val);
        if (val_len >= MAX_HEADER_VAL)
            return PARSE_ERROR;
        memcpy(h->value, val, val_len);
        h->value[val_len] = '\0';

        req->header_count++;
        p = eol + 2;
    }

    /* ---- 4. Body pointer (may be empty) ---- */
    req->body = header_end + 4;
    req->body_len = (int)((buf + len) - req->body);
    if (req->body_len < 0)
        req->body_len = 0;

    return PARSE_OK;
}

const char *http_request_get_header(const http_request_t *req, const char *name) {
    char lower[MAX_HEADER_NAME];
    int i = 0;
    for (; name[i] && i < MAX_HEADER_NAME - 1; i++)
        lower[i] = (char)tolower((unsigned char)name[i]);
    lower[i] = '\0';

    for (int j = 0; j < req->header_count; j++) {
        if (strcmp(req->headers[j].name, lower) == 0)
            return req->headers[j].value;
    }
    return NULL;
}
