#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

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
	if (response.find("101 Switching Protocols") == string::npos) {
		cerr << "Handshake error: " << response << endl;
		return false;
	}
	if (response.find("Sec-WebSocket-Accept: hello, kitty") == string::npos) {
		cerr << "Handshake error: Accept key mismatch in response:\n" << response << endl;
		return false;
	}
	cout << "Minimal handshake successful." << endl;
	return true;
}

int main(int argc, char *argv[]) {
	if (argc < 3) {
		cerr << "Usage: " << argv[0] << " hostname port" << endl;
		exit(1);
	}
	string host = argv[1];
	int port = atoi(argv[2]);

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		error("ERROR opening socket");
	SocketRAII socketWrapper(sockfd);

	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	struct hostent *server = gethostbyname(host.c_str());
	if (server == nullptr) {
		cerr << "ERROR, no such host" << endl;
		exit(1);
	}
	memcpy((char *)&serv_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
	serv_addr.sin_port = htons(port);

	if (connect(socketWrapper.get(), reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr)) < 0)
		error("ERROR connecting");

	if (!MinimalPerformHandshakeClient(socketWrapper.get(), host, port, kFixedClientKey))
		exit(1);

	cout << "Handshake verified. Connection is now open and idle." << endl;

	// Бесконечно держим соединение открытым
	while (true) {
		pause();  // Или sleep(1)
	}

	return 0;
}
