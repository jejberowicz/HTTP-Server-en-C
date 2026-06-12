#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "io.h"
#include "tls.h"
#include "http_request.h"
#include "http_response.h"
#include "static_files.h"
#include "router.h"
#include "threadpool.h"

#define DEFAULT_PORT       8080
#define BACKLOG            128
#define BUFFER_SIZE        8192
#define STATIC_ROOT        "www"
#define NUM_THREADS        8
#define QUEUE_CAPACITY     256
#define KEEP_ALIVE_TIMEOUT 10

/* ---- Global state ---- */

static router_t   g_router;
static tls_ctx_t *g_tls_ctx = NULL;   /* NULL = plain HTTP */

/* ---- Built-in route handlers ---- */

static int handle_health(const http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_init(res, 200);
    http_response_add_header(res, "Content-Type", "application/json");
    res->body        = "{\"status\":\"ok\"}\n";
    res->body_len    = 16;
    res->use_chunked = 1;
    return 0;
}

/* ---- Socket helpers ---- */

static int create_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt"); close(fd); return -1;
    }

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY,
    };

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(fd); return -1;
    }
    if (listen(fd, BACKLOG) < 0) {
        perror("listen"); close(fd); return -1;
    }
    return fd;
}

/* ---- Connection handler ---- */

static int wants_keep_alive(const http_request_t *req) {
    const char *conn = http_request_get_header(req, "connection");
    if (conn) {
        if (strncasecmp(conn, "close",      5)  == 0) return 0;
        if (strncasecmp(conn, "keep-alive", 10) == 0) return 1;
    }
    return (strcmp(req->version, "HTTP/1.1") == 0);
}

void handle_connection(int client_fd, struct sockaddr_in *client_addr) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr->sin_addr, ip, sizeof(ip));
    printf("Connection from %s:%d%s\n",
           ip, ntohs(client_addr->sin_port), g_tls_ctx ? " (TLS)" : "");

    struct timeval tv = { .tv_sec = KEEP_ALIVE_TIMEOUT, .tv_usec = 0 };
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* TLS handshake (skipped for plain HTTP) */
    io_t io = { .fd = client_fd, .ssl = NULL };
    if (g_tls_ctx) {
        io.ssl = tls_accept(g_tls_ctx, client_fd);
        if (!io.ssl) { close(client_fd); return; }
    }

    char buf[BUFFER_SIZE];
    int  buf_len = 0;

    while (1) {
        /* Accumulate until we have a full header block */
        while (1) {
            ssize_t n = io_read(&io, buf + buf_len,
                                sizeof(buf) - 1 - (size_t)buf_len);
            if (n <= 0) goto done;
            buf_len += (int)n;
            buf[buf_len] = '\0';
            if (memmem(buf, (size_t)buf_len, "\r\n\r\n", 4)) break;
            if (buf_len >= (int)sizeof(buf) - 1) {
                /* Headers too large — tell the client instead of silently closing */
                http_response_t err;
                http_response_init(&err, 431);
                http_response_add_header(&err, "Content-Type", "text/plain");
                http_response_add_header(&err, "Connection",   "close");
                err.body     = "Request Header Fields Too Large\r\n";
                err.body_len = 33;
                http_response_send(&err, &io);
                goto done;
            }
        }

        http_request_t  req;
        http_response_t res;
        int body_needs_free = 0;

        parse_result_t result = http_request_parse(buf, buf_len, &req);

        if (result != PARSE_OK) {
            http_response_init(&res, 400);
            http_response_add_header(&res, "Content-Type", "text/plain");
            http_response_add_header(&res, "Connection",   "close");
            res.body     = "Bad Request\r\n";
            res.body_len = 13;
            http_response_send(&res, &io);
            goto done;
        }

        printf("%s %s %s  (%d headers)\n",
               req.method, req.path, req.version, req.header_count);

        int keep_alive = wants_keep_alive(&req);

        int matched = router_dispatch(&g_router, &req, &res);
        if (matched == 0)
            static_file_serve(STATIC_ROOT, &req, &res, &body_needs_free);
        if (matched < 0) {
            http_response_init(&res, 500);
            http_response_add_header(&res, "Content-Type", "text/plain");
            res.body     = "Internal Server Error\r\n";
            res.body_len = 22;
            keep_alive   = 0;
        }

        http_response_add_header(&res, "Connection",
                                 keep_alive ? "keep-alive" : "close");

        if (res.use_chunked)
            http_response_send_chunked(&res, &io);
        else
            http_response_send(&res, &io);

        if (body_needs_free)
            free((void *)res.body);

        if (!keep_alive) goto done;

        /* Skip request body, keep pipelined bytes */
        const char *cl = http_request_get_header(&req, "content-length");
        long content_length = 0;
        if (cl) {
            char *end;
            content_length = strtol(cl, &end, 10);
            /* Reject missing, negative, or absurdly large values */
            if (end == cl || content_length < 0 || content_length > 10 * 1024 * 1024)
                content_length = 0;
        }
        const char *hdr_end = (const char *)memmem(buf, (size_t)buf_len,
                                                    "\r\n\r\n", 4);
        if (!hdr_end) goto done;   /* shouldn't happen, but guard UB */
        int consumed = (int)(hdr_end - buf) + 4 + (int)content_length;
        if (consumed < 0 || consumed > buf_len) consumed = buf_len;
        buf_len -= consumed;
        if (buf_len > 0)
            memmove(buf, buf + consumed, (size_t)buf_len);
    }

done:
    if (io.ssl) tls_session_free(io.ssl);
    close(client_fd);
}

/* ---- main ---- */

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [port] [--tls cert.pem key.pem]\n"
            "  port              TCP port to listen on (default: %d)\n"
            "  --tls cert key    Enable TLS with given certificate and key\n",
            prog, DEFAULT_PORT);
}

int main(int argc, char *argv[]) {
    int         port     = DEFAULT_PORT;
    const char *cert     = NULL;
    const char *key      = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tls") == 0) {
            if (i + 2 >= argc) { print_usage(argv[0]); return 1; }
            cert = argv[++i];
            key  = argv[++i];
        } else {
            port = atoi(argv[i]);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "Invalid port: %s\n", argv[i]);
                return 1;
            }
        }
    }

    if (cert && key) {
        g_tls_ctx = tls_init(cert, key);
        if (!g_tls_ctx) return 1;
    }

    router_init(&g_router);
    router_add(&g_router, "GET", "/health", MATCH_EXACT, handle_health);

    threadpool_t *pool = threadpool_create(NUM_THREADS, QUEUE_CAPACITY);
    if (!pool) { fprintf(stderr, "Failed to create thread pool\n"); return 1; }

    int server_fd = create_server_socket(port);
    if (server_fd < 0) { threadpool_destroy(pool); return 1; }

    printf("Listening on port %d  (workers: %d, TLS: %s)\n",
           port, NUM_THREADS, g_tls_ctx ? "yes" : "no");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        if (threadpool_submit(pool, client_fd, &client_addr) < 0)
            close(client_fd);
    }

    close(server_fd);
    threadpool_destroy(pool);
    if (g_tls_ctx) tls_ctx_free(g_tls_ctx);
    return 0;
}
