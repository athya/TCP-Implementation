default: server client

server: server.cpp header.h
	g++ -g -std=c++11 -o server server.cpp header.cpp

client: client.cpp header.h
	g++ -g -std=c++11 -o client client.cpp header.cpp