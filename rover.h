//ROVER CLASS

#ifndef ROVER_H
#define ROVER_H
#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

class Rover {
private:
    int broadcast_port;
    int listen_port;
    SOCKET listen_socket;
    struct sockaddr_in listen_addr;
    bool running;

public:
    Rover(int broadcast_port, int listen_port);
    ~Rover();
    bool sendMessage(const std::string& ip, int port, const std::string& message); // Msg another rover
    bool broadcastMessage(const std::string& message); // Broadcast to all rovers
    bool startListening(); // Start listening for connections
    void stopListening(); // Stopl istening
};

#endif // ROVER_H