#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ctime>
#include <vector>
#include <cstdint>

using namespace std;

#define BUFFER_SIZE 2048
const string kFixedClientKey = "hello, world";

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

bool MinimalPerformHandshakeClient(int sockfd, const string &host, int port, const string &client_key) {
    ostringstream oss;
    oss << "GET / HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << client_key << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n\r\n";
    string request = oss.str();
    if (write(sockfd, request.c_str(), request.size()) < 0)
        error("ERROR writing handshake request");

    char buffer[BUFFER_SIZE];
    int n = read(sockfd, buffer, sizeof(buffer) - 1);
    if (n <= 0)
        error("ERROR reading handshake response");
    buffer[n] = '\0';
    string response(buffer);
    if (response.find("101 Switching Protocols") == string::npos ||
        response.find("Sec-WebSocket-Accept: hello, kitty") == string::npos) {
        cerr << "Handshake error:\n" << response << endl;
        return false;
    }
    cout << "Minimal handshake successful." << endl;
    return true;
}

void SendWebSocketText(int sockfd, const string &message) {
    uint8_t masking_key[4];
    srand(time(nullptr));
    for (int i = 0; i < 4; i++) masking_key[i] = rand() & 0xFF;

    vector<uint8_t> frame;
    frame.push_back(0x81);  // FIN=1, opcode=1

    size_t len = message.size();
    if (len <= 125) {
        frame.push_back(0x80 | (uint8_t)len);
    } else if (len <= 0xFFFF) {
        frame.push_back(0x80 | 126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back((len >> (i * 8)) & 0xFF);
        }
    }

    // mask key
    frame.insert(frame.end(), masking_key, masking_key + 4);
    // masked payload
    for (size_t i = 0; i < len; i++) {
        frame.push_back(message[i] ^ masking_key[i % 4]);
    }

    write(sockfd, frame.data(), frame.size());
}

string ReceiveWebSocketText(int sockfd) {
    uint8_t header[2];
    if (read(sockfd, header, 2) != 2) return "";

    uint64_t len = header[1] & 0x7F;
    if (len == 126) {
        uint8_t ext[2];
        read(sockfd, ext, 2);
        len = (ext[0] << 8) | ext[1];
    } else if (len == 127) {
        uint8_t ext[8];
        read(sockfd, ext, 8);
        len = 0;
        for (int i = 0; i < 8; i++) {
            len = (len << 8) | ext[i];
        }
    }

    // server frames are not masked
    string result;
    result.reserve(len);
    for (uint64_t i = 0; i < len; i++) {
        char c;
        if (read(sockfd, &c, 1) != 1) break;
        result.push_back(c);
    }
    return result;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " hostname port" << endl;
        exit(1);
    }
    string host = argv[1];
    int port = atoi(argv[2]);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");
    SocketRAII socketWrapper(sockfd);

    hostent *server = gethostbyname(host.c_str());
    if (!server) {
        cerr << "ERROR, no such host" << endl;
        exit(1);
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    if (connect(socketWrapper.get(), (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR connecting");

    if (!MinimalPerformHandshakeClient(socketWrapper.get(), host, port, kFixedClientKey))
        exit(1);

    cout << "Connection open. Enter commands (ping, time, close):" << endl;
    string cmd;
    while (true) {
        cout << "> ";
        if (!getline(cin, cmd)) break;
        SendWebSocketText(socketWrapper.get(), cmd);
        if (cmd == "close") break;
        string reply = ReceiveWebSocketText(socketWrapper.get());
        if (reply.empty()) {
            cerr << "Server closed connection." << endl;
            break;
        }
        cout << "Server: " << reply << endl;
    }

    return 0;
}
