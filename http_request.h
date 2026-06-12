#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#define MAX_HEADERS     64
#define MAX_METHOD_LEN  16
#define MAX_PATH_LEN    2048
#define MAX_VERSION_LEN 16
#define MAX_HEADER_NAME 256
#define MAX_HEADER_VAL  4096

typedef struct {
    char name[MAX_HEADER_NAME];
    char value[MAX_HEADER_VAL];
} http_header_t;

typedef struct {
    char method[MAX_METHOD_LEN];
    char path[MAX_PATH_LEN];
    char version[MAX_VERSION_LEN];
    http_header_t headers[MAX_HEADERS];
    int  header_count;
    const char *body;   /* points into the original buffer */
    int  body_len;
} http_request_t;

typedef enum {
    PARSE_OK = 0,
    PARSE_INCOMPLETE,   /* need more data                  */
    PARSE_ERROR,        /* malformed request               */
} parse_result_t;

/*
 * Parse a raw HTTP/1.x request stored in `buf` (length `len`).
 * On success (PARSE_OK) `req` is filled in.
 */
parse_result_t http_request_parse(const char *buf, int len, http_request_t *req);

/* Return the value of a header by name (case-insensitive), or NULL. */
const char *http_request_get_header(const http_request_t *req, const char *name);

#endif /* HTTP_REQUEST_H */
