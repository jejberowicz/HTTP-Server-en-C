#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <stddef.h>
#include "io.h"

#define MAX_RESP_HEADERS  32
#define MAX_RESP_HDR_LINE 512

typedef struct {
    int  status_code;
    char headers[MAX_RESP_HEADERS][MAX_RESP_HDR_LINE];
    int  header_count;
    const char *body;
    int  body_len;
    int  use_chunked;   /* send with Transfer-Encoding: chunked */
} http_response_t;

void        http_response_init(http_response_t *res, int status_code);
void        http_response_add_header(http_response_t *res,
                                     const char *name, const char *value);
/*
 * Serialise the response into `buf` (capacity `cap`).
 * Returns the number of bytes written, or -1 if the buffer is too small.
 */
int         http_response_write(const http_response_t *res,
                                char *buf, int cap);

/* Convenience: send a complete response over an io_t connection. */
int         http_response_send(const http_response_t *res, io_t *io);

/*
 * Send headers with Transfer-Encoding: chunked, then stream `body`
 * as a single chunk followed by the terminating 0-length chunk.
 * Use this when Content-Length is unknown at response-build time.
 */
int         http_response_send_chunked(const http_response_t *res, io_t *io);

/* Human-readable reason phrase for common status codes. */
const char *http_status_phrase(int code);

#endif /* HTTP_RESPONSE_H */
