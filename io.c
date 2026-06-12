#include <unistd.h>
#include <openssl/ssl.h>
#include "io.h"

ssize_t io_read(io_t *io, void *buf, size_t len) {
    if (io->ssl)
        return (ssize_t)SSL_read((SSL *)io->ssl, buf, (int)len);
    return read(io->fd, buf, len);
}

ssize_t io_write(io_t *io, const void *buf, size_t len) {
    if (io->ssl)
        return (ssize_t)SSL_write((SSL *)io->ssl, buf, (int)len);
    return write(io->fd, buf, len);
}
