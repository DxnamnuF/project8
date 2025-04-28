#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <ctime>
#include <signal.h>

using namespace std;

const int kPort = 9090;
const int kBufferSize = 2048;

const string kFixedClientKey = "hello, world";
const string kFixedAcceptKey = "hello, kitty";

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

void error(const string &msg) {
    perror(msg.c_str());
    exit(1);
}

string GetCommandResponse(const string &message) {
    if (message == "ping")
        return "pong";
    else if (message == "time") {
        time_t t = time(nullptr);
        char buf[64];
        struct tm *tm_info = localtime(&t);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
        return string("Current time: ") + buf;
    }
    return "Unknown command: " + message;
}

bool MinimalPerformHandshake(int client_sock) {
    char buffer[kBufferSize];
    int n = recv(client_sock, buffer, kBufferSize - 1, 0);
    if (n <= 0) {
        cerr << "[HANDSHAKE ERROR] Failed to read request." << endl;
        return false;
    }
    buffer[n] = '\0';
    string request(buffer);

    if (request.find("Upgrade: websocket") == string::npos) {
        string html = "<html><body><h1>Not a WebSocket request</h1></body></html>";
        ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: text/html\r\n"
            << "Content-Length: " << html.size() << "\r\n"
            << "Connection: close\r\n\r\n"
            << html;
        string response = oss.str();
        send(client_sock, response.c_str(), response.size(), 0);
        return false;
    }

    size_t pos = request.find("Sec-WebSocket-Key:");
    if (pos == string::npos) {
        cerr << "[HANDSHAKE ERROR] Missing Sec-WebSocket-Key header." << endl;
        return false;
    }
    pos += strlen("Sec-WebSocket-Key:");
    while (pos < request.size() && request[pos] == ' ') pos++;
    size_t endPos = request.find("\r\n", pos);
    string client_key = request.substr(pos, endPos - pos);

    if (client_key != kFixedClientKey) {
        cerr << "[HANDSHAKE ERROR] Unexpected client key: " << client_key << endl;
        return false;
    }

    ostringstream oss;
    oss << "HTTP/1.1 101 Switching Protocols\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Accept: " << kFixedAcceptKey << "\r\n\r\n";
    string response = oss.str();
    if (send(client_sock, response.c_str(), response.size(), 0) <= 0) {
        cerr << "[HANDSHAKE ERROR] Failed to send handshake response." << endl;
        return false;
    }
    cout << "[HANDSHAKE] Handshake successful. Accept key: " << kFixedAcceptKey << endl;
    return true;
}

void ProcessWebSocketMessages(int client_sock, const string &clientID) {
    char buffer[kBufferSize];
    while (true) {
        int bytes_received = read(client_sock, buffer, sizeof(buffer));
        if (bytes_received <= 0)
            break;

        unsigned char opcode = buffer[0] & 0x0F;
        if (opcode == 0x8)  // close frame
            break;
        if (opcode != 0x1)  // only text frames
            continue;

        int payload_len = buffer[1] & 0x7F;
        int offset = 2;
        if (payload_len == 126) {
            payload_len = ((unsigned char)buffer[2] << 8) | (unsigned char)buffer[3];
            offset += 2;
        } else if (payload_len == 127) {
            payload_len = 0;
            for (int j = 0; j < 8; j++) {
                payload_len = (payload_len << 8) | (unsigned char)buffer[offset + j];
            }
            offset += 8;
        }

        unsigned char masking_key[4];
        memcpy(masking_key, buffer + offset, 4);
        offset += 4;

        string decoded;
        for (int i = 0; i < payload_len && (offset + i) < kBufferSize; i++) {
            decoded.push_back(buffer[offset + i] ^ masking_key[i % 4]);
        }
        cout << "[CLIENT " << clientID << "] Received: " << decoded << endl;

        if (decoded == "close") {
            // send close frame
            char close_frame[2] = { static_cast<char>(0x88), 0x00 };
            write(client_sock, close_frame, 2);
            break;
        }

        string reply = GetCommandResponse(decoded);

        // build response frame (unmasked)
        string frame;
        frame.push_back(static_cast<char>(0x81));
        if (reply.size() < 126) {
            frame.push_back(static_cast<char>(reply.size()));
        } else if (reply.size() < 65536) {
            frame.push_back(126);
            frame.push_back((reply.size() >> 8) & 0xFF);
            frame.push_back(reply.size() & 0xFF);
        } else {
            frame.push_back(127);
            for (int j = 7; j >= 0; j--) {
                frame.push_back((reply.size() >> (j * 8)) & 0xFF);
            }
        }
        frame += reply;
        write(client_sock, frame.c_str(), frame.size());
    }

    // Логируем отключение клиента
    cout << "[INFO] Client " << clientID << " disconnected." << endl;
}

void SigChildHandler(int) {
    while (waitpid(-1, nullptr, WNOHANG) > 0) {}
}

int main() {
    signal(SIGCHLD, SigChildHandler);

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) error("ERROR opening socket");
    SocketRAII listeningSocket(server_sock);

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(kPort);

    if (bind(listeningSocket.get(), (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");
    if (listen(listeningSocket.get(), 10) < 0)
        error("ERROR on listen");

    cout << "[SERVER] WebSocket server listening on port " << kPort << endl;

    while (true) {
        sockaddr_in cli_addr{};
        socklen_t clilen = sizeof(cli_addr);
        int client_sock = accept(listeningSocket.get(), (sockaddr*)&cli_addr, &clilen);
        if (client_sock < 0) {
            perror("ERROR on accept");
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            close(listeningSocket.get());
            string clientID = inet_ntoa(cli_addr.sin_addr);
            cout << "[INFO] New connection from " << clientID << endl;
            if (MinimalPerformHandshake(client_sock)) {
                cout << "[INFO] Handshake with " << clientID << " succeeded." << endl;
                ProcessWebSocketMessages(client_sock, clientID);
            } else {
                cout << "[INFO] Handshake with " << clientID << " failed." << endl;
            }
            close(client_sock);
            exit(0);
        } else if (pid < 0) {
            perror("ERROR on fork");
        } else {
            close(client_sock);
        }
    }

    return 0;
}
