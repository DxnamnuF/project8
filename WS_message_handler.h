#ifndef WS_MESSAGE_HANDLER_H
#define WS_MESSAGE_HANDLER_H

#include <string>
#include <openssl/ssl.h>

class WebSocketMessageHandler {
public:
    static void sendClientTextMessage(SSL* ssl, const std::string& message);
    static void sendServerTextMessage(SSL* ssl, const std::string& message);
    static std::string receiveTextMessage(SSL* ssl);
};

#endif
