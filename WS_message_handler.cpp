#include "WS_message_handler.h"
#include <openssl/ssl.h>
#include <cstring>
#include <ctime>
#include <cstdlib>

#define BUFFER_SIZE 2048

// Клиентская реализация
void WebSocketMessageHandler::sendClientTextMessage(SSL* ssl, const std::string& message) {
    unsigned char masking_key[4];
    srand(time(nullptr));
    for (int i = 0; i < 4; i++) masking_key[i] = rand() & 0xFF;

    char frame[BUFFER_SIZE];
    int frame_idx = 0;
    frame[frame_idx++] = 0x81; // FIN=1, Text frame (opcode=1)

    size_t len = message.size();
    if (len <= 125) {
        frame[frame_idx++] = 0x80 | (uint8_t)len; // Masked + length
    } else if (len <= 0xFFFF) {
        frame[frame_idx++] = 0x80 | 126;
        frame[frame_idx++] = (len >> 8) & 0xFF;
        frame[frame_idx++] = len & 0xFF;
    } else {
        frame[frame_idx++] = 0x80 | 127;
        for (int i = 7; i >= 0; --i) {
            frame[frame_idx++] = (len >> (8 * i)) & 0xFF;
        }
    }

    // Masking key
    memcpy(&frame[frame_idx], masking_key, 4);
    frame_idx += 4;

    // Masked payload
    for (size_t i = 0; i < len; ++i) {
        frame[frame_idx++] = message[i] ^ masking_key[i % 4];
    }

    SSL_write(ssl, frame, frame_idx);
}

// Серверная реализация — не маскирует
void WebSocketMessageHandler::sendServerTextMessage(SSL* ssl, const std::string& message) {
    char frame[BUFFER_SIZE];
    int frame_idx = 0;
    frame[frame_idx++] = 0x81; // FIN=1, Text frame (opcode=1)

    size_t len = message.size();
    if (len <= 125) {
        frame[frame_idx++] = (uint8_t)len;
    } else if (len <= 0xFFFF) {
        frame[frame_idx++] = 126;
        frame[frame_idx++] = (len >> 8) & 0xFF;
        frame[frame_idx++] = len & 0xFF;
    } else {
        frame[frame_idx++] = 127;
        for (int i = 7; i >= 0; --i) {
            frame[frame_idx++] = (len >> (8 * i)) & 0xFF;
        }
    }

    memcpy(&frame[frame_idx], message.c_str(), len);
    frame_idx += len;

    SSL_write(ssl, frame, frame_idx);
}

//  Общая функция чтения (подходит и для клиента, и сервера)
std::string WebSocketMessageHandler::receiveTextMessage(SSL* ssl) {
    unsigned char header[2];
    if (SSL_read(ssl, header, 2) != 2) return "";

    uint64_t len = header[1] & 0x7F;
    if (len == 126) {
        unsigned char ext[2];
        SSL_read(ssl, ext, 2);
        len = (ext[0] << 8) | ext[1];
    } else if (len == 127) {
        unsigned char ext[8];
        SSL_read(ssl, ext, 8);
        len = 0;
        for (int i = 0; i < 8; i++) {
            len = (len << 8) | ext[i];
        }
    }

    // Проверка на наличие masking key
    bool masked = (header[1] & 0x80);
    unsigned char masking_key[4];
    if (masked) SSL_read(ssl, masking_key, 4);

    std::string result;
    result.reserve(len);
    for (uint64_t i = 0; i < len; ++i) {
        char c;
        if (SSL_read(ssl, &c, 1) != 1) break;
        result.push_back(masked ? (c ^ masking_key[i % 4]) : c);
    }

    return result;
}
