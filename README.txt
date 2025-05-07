Projekt: WebSocket (WSS) – Serwer i Klient w C++ z użyciem OpenSSL
==================================================================

Opis:
-----
Ten projekt realizuje bezpieczną komunikację WebSocket (WSS) pomiędzy klientem a serwerem przy użyciu C++ i biblioteki OpenSSL.

Struktura plików:
-----------------
- WS_server.cpp           — Serwer WebSocket z TLS i obsługą wielu klientów
- WS_client.cpp           — Klient WebSocket z handshake TLS
- SSL_manager.h / .cpp    — Klasa do zarządzania SSL i certyfikatami
- WS_message_handler.h/.cpp — Obsługa ramek WebSocket (wysyłanie i odbieranie)

Wymagane pliki:
---------------
- server_cert.pem         — certyfikat X.509 serwera
- server_key.pem          — klucz prywatny serwera

Kompilacja:
-----------
g++ -o server WS_server.cpp SSL_manager.cpp WS_message_handler.cpp -lssl -lcrypto -pthread
g++ -o client WS_client.cpp WS_message_handler.cpp -lssl -lcrypto -pthread

Uruchamianie:
-------------
./server 9090                — uruchamia serwer na porcie 9090
./client localhost 9090     — łączy się z serwerem pod localhost:9090

Dostępne polecenia klienta:
---------------------------
ping     — serwer odpowiada "pong"
time     — serwer wysyła bieżący czas
close    — zamknięcie połączenia

Przykład logów serwera:
-----------------------
[SERVER] Listening on port 9090
[INFO] Client 127.0.0.1:51432 connected via WebSocket (TLS).
[CLIENT 127.0.0.1:51432] ping
[CLIENT 127.0.0.1:51432] time
[INFO] Client 127.0.0.1:51432 disconnected.

Uwagi:
------
Serwer nie maskuje danych — zgodnie z RFC 6455.
Klient maskuje dane — zgodnie z wymaganiami WebSocket.
Kanał jest szyfrowany przy pomocy SSL/TLS.

