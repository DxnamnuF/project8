#include "SSL_manager.h"
#include <openssl/err.h>
#include <iostream>
#include <cstdlib>

SSLManager::SSLManager(const std::string& certFile, const std::string& keyFile) {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) handleError("Unable to create SSL context");

    if (SSL_CTX_use_certificate_file(ctx, certFile.c_str(), SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file(ctx, keyFile.c_str(), SSL_FILETYPE_PEM) <= 0 ||
        !SSL_CTX_check_private_key(ctx)) {
        handleError("Invalid certificate or key");
    }
}

SSLManager::~SSLManager() {
    SSL_CTX_free(ctx);
}

SSL* SSLManager::createSSL(int sockfd) {
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sockfd);
    return ssl;
}

void SSLManager::handleError(const std::string& msg) {
    ERR_print_errors_fp(stderr);
    std::cerr << "[SSLManager ERROR] " << msg << std::endl;
    exit(EXIT_FAILURE);
}
