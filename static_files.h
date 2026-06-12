#ifndef STATIC_FILES_H
#define STATIC_FILES_H

#include "http_request.h"
#include "http_response.h"

/*
 * Serve a static file from `root_dir` matching req->path.
 * Fills `res` and returns 0 on success, -1 on internal error.
 * The caller must free res->body with free() when body_needs_free is set.
 */
int static_file_serve(const char *root_dir,
                      const http_request_t *req,
                      http_response_t *res,
                      int *body_needs_free);

/* Map a file extension to a MIME type string. */
const char *mime_type(const char *path);

#endif /* STATIC_FILES_H */
