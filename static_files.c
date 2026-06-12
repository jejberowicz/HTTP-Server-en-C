#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "static_files.h"

const char *mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext)
        return "application/octet-stream";
    ext++;  /* skip the dot */

    if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0)
        return "text/html";
    if (strcmp(ext, "css")  == 0) return "text/css";
    if (strcmp(ext, "js")   == 0) return "application/javascript";
    if (strcmp(ext, "json") == 0) return "application/json";
    if (strcmp(ext, "txt")  == 0) return "text/plain";
    if (strcmp(ext, "png")  == 0) return "image/png";
    if (strcmp(ext, "jpg")  == 0 || strcmp(ext, "jpeg") == 0)
        return "image/jpeg";
    if (strcmp(ext, "gif")  == 0) return "image/gif";
    if (strcmp(ext, "svg")  == 0) return "image/svg+xml";
    if (strcmp(ext, "ico")  == 0) return "image/x-icon";
    if (strcmp(ext, "pdf")  == 0) return "application/pdf";

    return "application/octet-stream";
}

/* Returns 1 if path contains a traversal sequence. */
static int has_traversal(const char *path) {
    /* Reject any ".." component */
    const char *p = path;
    while (*p) {
        if (p[0] == '.' && p[1] == '.' &&
            (p[2] == '/' || p[2] == '\0'))
            return 1;
        p++;
    }
    return 0;
}

static void set_error(http_response_t *res, int code, const char *msg) {
    http_response_init(res, code);
    http_response_add_header(res, "Content-Type", "text/plain");
    res->body     = msg;
    res->body_len = (int)strlen(msg);
}

int static_file_serve(const char *root_dir,
                      const http_request_t *req,
                      http_response_t *res,
                      int *body_needs_free) {
    *body_needs_free = 0;

    /* Only handle GET and HEAD */
    if (strcmp(req->method, "GET") != 0 && strcmp(req->method, "HEAD") != 0) {
        set_error(res, 405, "Method Not Allowed\r\n");
        return 0;
    }

    /* Strip query string */
    char path[2048];
    strncpy(path, req->path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    char *qs = strchr(path, '?');
    if (qs) *qs = '\0';

    /* Guard against path traversal */
    if (has_traversal(path)) {
        set_error(res, 403, "Forbidden\r\n");
        return 0;
    }

    /* Build filesystem path */
    char fspath[4096];
    int n = snprintf(fspath, sizeof(fspath), "%s%s", root_dir, path);
    if (n < 0 || n >= (int)sizeof(fspath)) {
        set_error(res, 500, "Internal Server Error\r\n");
        return -1;
    }

    /* If path ends with '/', try index.html */
    if (fspath[n - 1] == '/') {
        strncat(fspath, "index.html", sizeof(fspath) - strlen(fspath) - 1);
    }

    /* Stat the file */
    struct stat st;
    if (stat(fspath, &st) < 0) {
        set_error(res, 404, "Not Found\r\n");
        return 0;
    }

    /* If it's a directory, redirect to path/ */
    if (S_ISDIR(st.st_mode)) {
        http_response_init(res, 301);
        /* Build Location header: original path + "/" */
        static char location[2100];
        snprintf(location, sizeof(location), "%s/", req->path);
        http_response_add_header(res, "Location", location);
        set_error(res, 301, "Moved Permanently\r\n");
        return 0;
    }

    /* Read the file */
    FILE *f = fopen(fspath, "rb");
    if (!f) {
        set_error(res, 404, "Not Found\r\n");
        return 0;
    }

    char *body = malloc((size_t)st.st_size);
    if (!body) {
        fclose(f);
        set_error(res, 500, "Internal Server Error\r\n");
        return -1;
    }

    if (fread(body, 1, (size_t)st.st_size, f) != (size_t)st.st_size) {
        fclose(f);
        free(body);
        set_error(res, 500, "Internal Server Error\r\n");
        return -1;
    }
    fclose(f);

    http_response_init(res, 200);
    http_response_add_header(res, "Content-Type", mime_type(fspath));

    res->body        = body;
    res->body_len    = (int)st.st_size;
    *body_needs_free = 1;

    return 0;
}
