#!/bin/bash
./redis_server &
SERVER_PID=$!
sleep 1

# Send a SET command
python3 -c "import socket; s = socket.socket(); s.connect(('127.0.0.1', 6379)); s.sendall(b'*3\r\n\$3\r\nSET\r\n\$4\r\nname\r\n\$5\r\nalice\r\n'); print(s.recv(1024))"

# Kill it
kill -9 $SERVER_PID
sleep 1

echo "Restarting server to test AOF replay..."
./redis_server &
SERVER_PID=$!
sleep 1

# Send a GET command
python3 -c "import socket; s = socket.socket(); s.connect(('127.0.0.1', 6379)); s.sendall(b'*2\r\n\$3\r\nGET\r\n\$4\r\nname\r\n'); print(s.recv(1024))"

kill -9 $SERVER_PID
