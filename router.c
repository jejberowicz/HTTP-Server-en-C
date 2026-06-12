#include <string.h>
#include "router.h"

void router_init(router_t *r) {
    memset(r, 0, sizeof(*r));
}

void router_add(router_t *r, const char *method, const char *pattern,
                match_type_t match_type, route_handler_t handler) {
    if (r->count >= MAX_ROUTES)
        return;

    route_t *rt = &r->routes[r->count++];

    if (method)
        strncpy(rt->method, method, sizeof(rt->method) - 1);
    else
        rt->method[0] = '\0';   /* empty = wildcard */

    strncpy(rt->pattern, pattern, sizeof(rt->pattern) - 1);
    rt->match_type = match_type;
    rt->handler    = handler;
}

int router_dispatch(const router_t *r, const http_request_t *req,
                    http_response_t *res) {
    for (int i = 0; i < r->count; i++) {
        const route_t *rt = &r->routes[i];

        /* Method check (empty = wildcard) */
        if (rt->method[0] != '\0' &&
            strcmp(rt->method, req->method) != 0)
            continue;

        /* Path check */
        int matched = 0;
        if (rt->match_type == MATCH_EXACT) {
            matched = (strcmp(rt->pattern, req->path) == 0);
        } else {
            size_t plen = strlen(rt->pattern);
            matched = (strncmp(rt->pattern, req->path, plen) == 0);
        }

        if (!matched)
            continue;

        return rt->handler(req, res) == 0 ? 1 : -1;
    }
    return 0;  /* no route matched */
}
