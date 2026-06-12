#ifndef TLS_H
#define TLS_H

/* Opaque TLS server context. */
typedef struct tls_ctx tls_ctx_t;

/*
 * Load a certificate + private key and create a server SSL_CTX.
 * Returns NULL on failure (error printed to stderr).
 */
tls_ctx_t *tls_init(const char *cert_file, const char *key_file);

/*
 * Perform the TLS handshake on an accepted fd.
 * Returns an opaque SSL session pointer (cast to SSL * internally),
 * or NULL on failure.
 */
void *tls_accept(tls_ctx_t *ctx, int fd);

/* Shutdown and free a TLS session returned by tls_accept(). */
void tls_session_free(void *ssl);

/* Free the server context. */
void tls_ctx_free(tls_ctx_t *ctx);

#endif /* TLS_H */
