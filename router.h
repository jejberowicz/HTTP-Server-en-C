#ifndef ROUTER_H
#define ROUTER_H

#include "http_request.h"
#include "http_response.h"

#define MAX_ROUTES 64

/*
 * A handler receives the parsed request and a response to fill in.
 * Returns 0 on success, -1 on internal error.
 */
typedef int (*route_handler_t)(const http_request_t *req, http_response_t *res);

typedef enum {
    MATCH_EXACT,   /* path must equal the pattern exactly  */
    MATCH_PREFIX,  /* path must start with the pattern     */
} match_type_t;

typedef struct {
    char            method[16];
    char            pattern[2048];
    match_type_t    match_type;
    route_handler_t handler;
} route_t;

typedef struct {
    route_t routes[MAX_ROUTES];
    int     count;
} router_t;

void router_init(router_t *r);

/*
 * Register a route.  Use method = NULL to match any method.
 */
void router_add(router_t *r, const char *method, const char *pattern,
                match_type_t match_type, route_handler_t handler);

/*
 * Dispatch the request.
 * Returns  1 if a route matched (res is filled).
 * Returns  0 if no route matched (caller should fall back to static files).
 * Returns -1 on internal handler error.
 */
int router_dispatch(const router_t *r, const http_request_t *req,
                    http_response_t *res);

#endif /* ROUTER_H */
