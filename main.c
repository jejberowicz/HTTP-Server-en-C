#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "http_request.h"
#include "http_response.h"
#include "static_files.h"
#include <sys/time.h>
#include "router.h"
#include "threadpool.h"

#define DEFAULT_PORT       8080
#define BACKLOG            128
#define BUFFER_SIZE        8192
#define STATIC_ROOT        "www"
#define NUM_THREADS        8
#define QUEUE_CAPACITY     256
#define KEEP_ALIVE_TIMEOUT 10   /* seconds before an idle connection is closed */

/* ---- Built-in route handlers ---- */

static int handle_health(const http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_init(res, 200);
    http_response_add_header(res, "Content-Type", "application/json");
    res->body       = "{\"status\":\"ok\"}\n";
    res->body_len   = 16;
    res->use_chunked = 1;
    return 0;
}


static router_t g_router;

static int create_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(fd);
        return -1;
    }

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY,
    };

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, BACKLOG) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    return fd;
}

/* Returns 1 if the request wants keep-alive, 0 if close. */
static int wants_keep_alive(const http_request_t *req) {
    const char *conn = http_request_get_header(req, "connection");
    if (conn) {
        if (strncasecmp(conn, "close",      5) == 0) return 0;
        if (strncasecmp(conn, "keep-alive", 10) == 0) return 1;
    }
    /* HTTP/1.1 defaults to keep-alive; HTTP/1.0 defaults to close */
    return (strcmp(req->version, "HTTP/1.1") == 0);
}

void handle_connection(int client_fd, struct sockaddr_in *client_addr) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr->sin_addr, ip, sizeof(ip));
    printf("Connection from %s:%d\n", ip, ntohs(client_addr->sin_port));

    /* Set receive timeout so idle keep-alive connections don't hang forever */
    struct timeval tv = { .tv_sec = KEEP_ALIVE_TIMEOUT, .tv_usec = 0 };
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buf[BUFFER_SIZE];
    int  buf_len = 0;

    while (1) {
        /* Accumulate data until we have a complete header section */
        while (1) {
            ssize_t n = recv(client_fd, buf + buf_len,
                             sizeof(buf) - 1 - (size_t)buf_len, 0);
            if (n <= 0) goto done;   /* timeout, EOF, or error */
            buf_len += (int)n;
            buf[buf_len] = '\0';
            if (memmem(buf, (size_t)buf_len, "\r\n\r\n", 4)) break;
            if (buf_len >= (int)sizeof(buf) - 1)      goto done;
        }

        http_request_t req;
        parse_result_t result = http_request_parse(buf, buf_len, &req);
        http_response_t res;
        int body_needs_free = 0;

        if (result != PARSE_OK) {
            http_response_init(&res, 400);
            http_response_add_header(&res, "Content-Type", "text/plain");
            http_response_add_header(&res, "Connection",   "close");
            res.body     = "Bad Request\r\n";
            res.body_len = 13;
            http_response_send(&res, client_fd);
            goto done;
        }

        printf("%s %s %s  (%d headers)\n",
               req.method, req.path, req.version, req.header_count);

        int keep_alive = wants_keep_alive(&req);

        /* 1. Try registered routes first */
        int matched = router_dispatch(&g_router, &req, &res);

        /* 2. Fall back to static files */
        if (matched == 0)
            static_file_serve(STATIC_ROOT, &req, &res, &body_needs_free);

        /* 3. Internal handler error → 500 */
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
            http_response_send_chunked(&res, client_fd);
        else
            http_response_send(&res, client_fd);

        if (body_needs_free)
            free((void *)res.body);

        if (!keep_alive) goto done;

        /* Consume the request body (if any) so the next read is clean */
        const char *cl = http_request_get_header(&req, "content-length");
        int content_length = cl ? atoi(cl) : 0;

        /* Bytes already in buffer after the header end */
        const char *hdr_end = (const char *)memmem(buf, buf_len, "\r\n\r\n", 4);
        int consumed = (int)(hdr_end - buf) + 4 + content_length;
        if (consumed > buf_len) consumed = buf_len;

        /* Shift any pipelined data to the front */
        buf_len -= consumed;
        if (buf_len > 0)
            memmove(buf, buf + consumed, (size_t)buf_len);
    }

done:
    close(client_fd);
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc == 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return 1;
        }
    }

    /* Register routes — unmatched requests fall through to static files */
    router_init(&g_router);
    router_add(&g_router, "GET", "/health", MATCH_EXACT, handle_health);

    threadpool_t *pool = threadpool_create(NUM_THREADS, QUEUE_CAPACITY);
    if (!pool) {
        fprintf(stderr, "Failed to create thread pool\n");
        return 1;
    }

    int server_fd = create_server_socket(port);
    if (server_fd < 0) {
        threadpool_destroy(pool);
        return 1;
    }

    printf("Listening on port %d  (workers: %d)\n", port, NUM_THREADS);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }

        if (threadpool_submit(pool, client_fd, &client_addr) < 0)
            close(client_fd);  /* pool shutting down */
    }

    close(server_fd);
    threadpool_destroy(pool);
    return 0;
}
