In the first teminal window:
./out-name port-number
for example:
./simple_server 9090
it will work and will display nothing, untill you start client. So it's ok if nothing happens.
In the next terminal windows:
./out-name ip port-number
for example:
./simple_client localhost 9090
Then you will see logs of every client:
"Minimal handshake successful.
Handshake verified. Connection is now open and idle."
in theclient windows and server logs
"[SERVER] Minimal WebSocket server listening on port 9090
[INFO] New connection from 127.0.0.1:48604
[HANDSHAKE] Handshake successful. Accept key: hello, kitty
[INFO] Handshake with client 127.0.0.1:48604 succeeded."
in the first window.
Then you can enter some commands in client window. ping zwraca pong, time - aktualny czas, a close - kończy połączenie. Server loguje otrzymane komandy, zamyka połączenie w przypadku close i loguje koniec połączenia.
