#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>
#include <ctime>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include "SSL_manager.h"
#include "WS_message_handler.h"

using namespace std;

#define BUFFER_SIZE 2048

void reapZombies(int) {
    while (waitpid(-1, nullptr, WNOHANG) > 0) {}
}

bool MinimalPerformHandshakeServer(SSL* ssl) {
    std::string request;
    char buffer[BUFFER_SIZE];
    int total_bytes = 0;

    while (true) {
        int n = SSL_read(ssl, buffer, sizeof(buffer) - 1);
        if (n <= 0) {
            cerr << "[HANDSHAKE ERROR] Failed to read handshake request" << endl;
            return false;
        }

        buffer[n] = '\0';
        request += buffer;
        total_bytes += n;

        if (request.find("\r\n\r\n") != std::string::npos)
            break;

        if (total_bytes > 8192) {
            cerr << "[HANDSHAKE ERROR] Request too large" << endl;
            return false;
        }
    }

    size_t key_pos = request.find("Sec-WebSocket-Key:");
    if (key_pos == string::npos) {
        cerr << "[HANDSHAKE ERROR] Missing Sec-WebSocket-Key" << endl;
        return false;
    }

    size_t start = key_pos + strlen("Sec-WebSocket-Key:");
    while (start < request.size() && request[start] == ' ') ++start;
    size_t end = request.find("\r\n", start);
    string client_key = request.substr(start, end - start);

    string accept_src = client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char sha1_result[20];
    SHA1(reinterpret_cast<const unsigned char*>(accept_src.c_str()), accept_src.size(), sha1_result);

    unsigned char base64_result[64];
    EVP_EncodeBlock(base64_result, sha1_result, 20);

    ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << base64_result << "\r\n\r\n";
    SSL_write(ssl, response.str().c_str(), response.str().size());
    return true;
}

class WebSocketServer {
public:
    WebSocketServer(int port, const string &certFile, const string &keyFile)
        : port(port), sslManager(certFile, keyFile) {}

    void start() {
        signal(SIGCHLD, reapZombies);
        int server_sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        serv_addr.sin_port = htons(port);
        bind(server_sock, (sockaddr*)&serv_addr, sizeof(serv_addr));
        listen(server_sock, 10);
        cout << "[SERVER] Listening on port " << port << endl;

        while (true) {
            sockaddr_in cli_addr{};
            socklen_t clilen = sizeof(cli_addr);
            int client_sock = accept(server_sock, (sockaddr*)&cli_addr, &clilen);
            if (fork() == 0) {
                close(server_sock);
                char client_ip_str[INET_ADDRSTRLEN];
				inet_ntop(AF_INET, &cli_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
				int client_port = ntohs(cli_addr.sin_port);
				ostringstream client_addr;
				client_addr << client_ip_str << ":" << client_port;
                SSL* ssl = sslManager.createSSL(client_sock);
                if (SSL_accept(ssl) <= 0 || !MinimalPerformHandshakeServer(ssl)) {
                    SSL_free(ssl); close(client_sock); exit(1);
                }
				cout << "[INFO] Client " << client_addr.str() << " connected via WebSocket (TLS)." << endl;
                while (true) {
                    string msg = WebSocketMessageHandler::receiveTextMessage(ssl);
                    if (msg.empty()) break;

                    // Логгирование команды
                    cout << "[CLIENT " << client_addr.str() << "] " << msg << endl;

                    if (msg == "ping") {
                        WebSocketMessageHandler::sendServerTextMessage(ssl, "pong");
                    } else if (msg == "time") {
                        time_t now = time(nullptr);
                        struct tm* tm_info = localtime(&now);
                        char time_buf[64];
                        strftime(time_buf, sizeof(time_buf), "%a %b %-d %H:%M:%S %Y", tm_info);
                        WebSocketMessageHandler::sendServerTextMessage(ssl, string(time_buf));
                    } else if (msg == "close") {
                        WebSocketMessageHandler::sendServerTextMessage(ssl, "bye");
                        cout << "[INFO] Client " << client_addr.str() << " disconnected." << endl;
                        break;
                    } else {
                        WebSocketMessageHandler::sendServerTextMessage(ssl, "Unknown command: " + msg);
                    }
                }

                SSL_shutdown(ssl);
                SSL_free(ssl);
                close(client_sock);
                exit(0);
            } else {
                close(client_sock);
            }
        }
    }

private:
    int port;
    SSLManager sslManager;
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <port>" << endl;
        return 1;
    }

    int port = atoi(argv[1]);
    WebSocketServer server(port, "server_cert.pem", "server_key.pem");
    server.start();
    return 0;
}
