#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include "SSL_manager.h"
#include "WS_message_handler.h"

using namespace std;

#define BUFFER_SIZE 2048

// Генерация случайного ключа и его Base64-кодирование
string generateWebSocketKey() {
    unsigned char random_key[16];
    RAND_bytes(random_key, sizeof(random_key));

    unsigned char base64_key[32];
    EVP_EncodeBlock(base64_key, random_key, sizeof(random_key));

    return string(reinterpret_cast<char*>(base64_key));
}

bool MinimalPerformHandshakeClient(SSL* ssl, const string& host, int port, const string& websocket_key) {
    ostringstream oss;
    oss << "GET / HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << websocket_key << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n\r\n";

    string request = oss.str();

    if (SSL_write(ssl, request.c_str(), request.size()) < 0)
        return false;

    char buffer[BUFFER_SIZE];
    int n = SSL_read(ssl, buffer, sizeof(buffer) - 1);
    if (n <= 0)
        return false;

    buffer[n] = '\0';
    string response(buffer);

    if (response.find("101 Switching Protocols") == string::npos) {
        cerr << "[ERROR] Handshake failed:\n" << response << endl;
        return false;
    }

    return true;
}

class SocketRAII {
public:
    explicit SocketRAII(int fd = -1) : sockfd(fd) { }
    ~SocketRAII() { if (sockfd != -1) close(sockfd); }
    int get() const { return sockfd; }
    SocketRAII(const SocketRAII&) = delete;
    SocketRAII& operator=(const SocketRAII&) = delete;
private:
    int sockfd;
};

void error(const string& msg) {
    perror(msg.c_str());
    exit(1);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <hostname> <port>" << endl;
        return 1;
    }

    string host = argv[1];
    int port = atoi(argv[2]);

    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) error("Unable to create SSL context");

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");
    SocketRAII socketWrapper(sockfd);

    hostent* server = gethostbyname(host.c_str());
    if (!server) error("ERROR, no such host");

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    if (connect(socketWrapper.get(), (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR connecting");

    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, socketWrapper.get());
    if (SSL_connect(ssl) <= 0) error("SSL_connect failed");

    string ws_key = generateWebSocketKey();
    if (!MinimalPerformHandshakeClient(ssl, host, port, ws_key))
        error("Handshake failed");

    cout << "Connection open. Enter commands (ping, time, close):" << endl;
    string cmd;
    while (true) {
        cout << "> ";
        if (!getline(cin, cmd)) break;

        WebSocketMessageHandler::sendClientTextMessage(ssl, cmd);
        if (cmd == "close") break;

        string reply = WebSocketMessageHandler::receiveTextMessage(ssl);
        if (reply.empty()) {
            cerr << "[INFO] Server closed connection." << endl;
            break;
        }

        cout << "Server: " << reply << endl;
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    return 0;
}
