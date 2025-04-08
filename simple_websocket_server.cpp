#include <iostream>      // std::cout, std::cerr
#include <sstream>       // std::ostringstream
#include <string>        // std::string
#include <cstring>       // memset, memcpy
#include <cstdlib>       // exit(), atoi()
#include <unistd.h>      // close(), recv(), send()
#include <sys/socket.h>  // socket(), bind(), listen(), accept()
#include <netinet/in.h>  // sockaddr_in, htons()
#include <arpa/inet.h>   // inet_ntoa()
#include <sys/wait.h>    // waitpid()
#include <ctime>         // time(), localtime(), strftime()

using namespace std;

// Параметры сервера.
const int kPort = 9090;
const int kBufferSize = 2048;

// Фиксированные ключи для handshake:
// Клиент должен отправлять "hello, world", а сервер отвечает "hello, kitty".
const string kFixedClientKey = "hello, world";
const string kFixedAcceptKey = "hello, kitty";

// RAII‑обёртка для сокета.
// При уничтожении объекта автоматически вызывается close() для освобождения дескриптора.
class SocketRAII {
public:
    explicit SocketRAII(int fd = -1) : sockfd(fd) { }
    ~SocketRAII() { if (sockfd != -1) close(sockfd); }
    int get() const { return sockfd; }
    // Запрещаем копирование, чтобы избежать дублирования дескриптора.
    SocketRAII(const SocketRAII&) = delete;
    SocketRAII& operator=(const SocketRAII&) = delete;
private:
    int sockfd; // Хранит файловый дескриптор сокета.
};

// Функция error – выводит сообщение об ошибке и завершает программу.
void error(const string &msg) {
    perror(msg.c_str());
    exit(1);
}

// Минимальная функция PerformHandshake.
// Считывает HTTP‑запрос, проверяет наличие "Upgrade: websocket", извлекает заголовок Sec‑WebSocket‑Key,
// сравнивает его с фиксированным значением "hello, world" и, если запрос корректный, отправляет
// handshake‑ответ с Accept‑ключом "hello, kitty".
bool MinimalPerformHandshake(int client_sock) {
    char buffer[kBufferSize];
    int n = recv(client_sock, buffer, kBufferSize - 1, 0);
    if (n <= 0) {
        cerr << "[HANDSHAKE ERROR] Failed to read request." << endl;
        return false;
    }
    buffer[n] = '\0'; // Гарантируем, что строка завершается нулевым символом.
    string request(buffer);

    // Если запрос не содержит "Upgrade: websocket", считаем, что это не WebSocket-запрос.
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
        close(client_sock);
        return false;
    }

    // Извлекаем заголовок "Sec-WebSocket-Key:".
    size_t pos = request.find("Sec-WebSocket-Key:");
    if (pos == string::npos) {
        cerr << "[HANDSHAKE ERROR] Missing Sec-WebSocket-Key header." << endl;
        return false;
    }
    pos += strlen("Sec-WebSocket-Key:");
    while (pos < request.size() && request[pos] == ' ') pos++;
    size_t endPos = request.find("\r\n", pos);
    string client_key = request.substr(pos, endPos - pos);
    // Сравниваем полученный ключ с ожидаемым фиксированным значением.
    if (client_key != kFixedClientKey) {
        cerr << "[HANDSHAKE ERROR] Unexpected client key: " << client_key << endl;
        return false;
    }

    // Формируем handshake-ответ.
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

int main() {
    // Создаем сокет для прослушивания входящих соединений.
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) error("ERROR opening socket");
    SocketRAII listeningSocket(server_sock); // Оборачиваем сокет в RAII‑класс.

    // Заполняем структуру адреса сервера.
    sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;         // IPv4.
    serv_addr.sin_addr.s_addr = INADDR_ANY;   // Принимаем соединения на всех интерфейсах.
    serv_addr.sin_port = htons(kPort);        // Порт 9090 в сетевом порядке.

    // Привязываем сокет к адресу.
    if (bind(listeningSocket.get(), reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    // Переводим сокет в режим прослушивания.
    if (listen(listeningSocket.get(), 10) < 0)
        error("ERROR on listen");

    cout << "[SERVER] Minimal WebSocket server listening on port " << kPort << endl;

    // Обрабатываем каждое входящее соединение последовательно (без fork).
    while (true) {
        sockaddr_in cli_addr;
        socklen_t clilen = sizeof(cli_addr);
        int client_sock = accept(listeningSocket.get(), reinterpret_cast<sockaddr*>(&cli_addr), &clilen);
        if (client_sock < 0) {
            error("ERROR on accept");
        }

        // Формируем идентификатор клиента: IP:порт.
        ostringstream oss;
        oss << inet_ntoa(cli_addr.sin_addr) << ":" << ntohs(cli_addr.sin_port);
        string clientID = oss.str();
        cout << "[INFO] New connection from " << clientID << endl;

        // Выполняем handshake для клиента.
        if (MinimalPerformHandshake(client_sock)) {
            cout << "[INFO] Handshake with client " << clientID << " succeeded." << endl;
        } else {
            cout << "[INFO] Handshake with client " << clientID << " failed." << endl;
        }
        // Закрываем соединение с клиентом.
        close(client_sock);
    }

    return 0;
}
