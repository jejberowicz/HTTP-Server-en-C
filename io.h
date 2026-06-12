#ifndef IO_H
#define IO_H

#include <sys/types.h>

/*
 * Thin I/O abstraction that works over a plain TCP fd or a TLS session.
 * ssl is NULL for plain connections; non-NULL for TLS (SSL * from OpenSSL).
 */
typedef struct {
    int   fd;
    void *ssl;   /* SSL * — kept as void* to avoid pulling openssl headers
                    into every translation unit that includes io.h        */
} io_t;

ssize_t io_read (io_t *io, void *buf, size_t len);
ssize_t io_write(io_t *io, const void *buf, size_t len);

#endif /* IO_H */
