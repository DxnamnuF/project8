#ifndef SSL_MANAGER_H
#define SSL_MANAGER_H

#include <openssl/ssl.h>
#include <string>

class SSLManager {
public:
    SSLManager(const std::string& certFile, const std::string& keyFile);
    ~SSLManager();

    SSL* createSSL(int sockfd);

private:
    SSL_CTX* ctx;
    void handleError(const std::string& msg);
};

#endif
