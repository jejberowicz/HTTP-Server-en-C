#include <stdio.h>
#include <stdlib.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "tls.h"

struct tls_ctx {
    SSL_CTX *ssl_ctx;
};

tls_ctx_t *tls_init(const char *cert_file, const char *key_file) {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    if (!SSL_CTX_check_private_key(ctx)) {
        fprintf(stderr, "TLS: certificate and key do not match\n");
        SSL_CTX_free(ctx);
        return NULL;
    }

    tls_ctx_t *tc = malloc(sizeof(*tc));
    if (!tc) { SSL_CTX_free(ctx); return NULL; }
    tc->ssl_ctx = ctx;
    return tc;
}

void *tls_accept(tls_ctx_t *ctx, int fd) {
    SSL *ssl = SSL_new(ctx->ssl_ctx);
    if (!ssl) return NULL;

    SSL_set_fd(ssl, fd);
    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }
    return ssl;
}

void tls_session_free(void *ssl) {
    if (!ssl) return;
    SSL_shutdown((SSL *)ssl);
    SSL_free((SSL *)ssl);
}

void tls_ctx_free(tls_ctx_t *ctx) {
    if (!ctx) return;
    SSL_CTX_free(ctx->ssl_ctx);
    free(ctx);
}
