#include "rover.h"
#include <iostream>
 // Rover class stuff
Rover::Rover(int broadcast_port, int listen_port)
    : broadcast_port(broadcast_port), listen_port(listen_port), running(false) {
    memset(&listen_addr, 0, sizeof(listen_addr)); // Listen socket address
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(listen_port);
    listen_addr.sin_addr.s_addr = INADDR_ANY;
} Rover::~Rover() {
    stopListening();
}
bool Rover::sendMessage(const std::string& ip, int port, const std::string& message) {
    SOCKET send_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &target_addr.sin_addr) <= 0) {
        std::cerr << "[ROVER ERROR] Invalid IP address: " << ip << std::endl;
        closesocket(send_socket);
        return false;
    }
    if (connect(send_socket,(struct sockaddr*)&target_addr, sizeof(target_addr)) == SOCKET_ERROR) {
        std::cerr <<"[ROVER ERROR] Failed to connect to rover at " << ip << ":" << port << ": " << WSAGetLastError() << std::endl;
        closesocket(send_socket);
        return false;
    }if(send(send_socket, message.c_str(), message.length(), 0) == SOCKET_ERROR) {
        std::cerr << "[ROVER ERROR] Failed to send message: " << WSAGetLastError() << std::endl;
        closesocket(send_socket);
        return false;
    }std::cout << "[ROVER] Message sent to " << ip << ":" << port << ": " << message << std::endl;
    closesocket(send_socket);
    return true;
}
bool Rover::broadcastMessage(const std::string& message) {
    SOCKET broadcast_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (broadcast_socket == INVALID_SOCKET){
        std::cerr << "[ROVER ERROR] coulnd't make broadcast socket: " << WSAGetLastError() << std::endl;
        return false;
    }
    BOOL broadcast_enabled = TRUE;
    if (setsockopt(broadcast_socket, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast_enabled, sizeof(broadcast_enabled)) == SOCKET_ERROR) {
        std::cerr << "[ROVER ERROR] Failed to set broadcast option: " << WSAGetLastError() << std::endl;
        closesocket(broadcast_socket);
        return false;
    }
    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(broadcast_port);
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255"); // Broadcast to all interfaces
    if (sendto(broadcast_socket, message.c_str(), message.length(), 0, (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr)) == SOCKET_ERROR) {
        std::cerr << "[ROVER ERROR] Failed to send broadcast: " << WSAGetLastError() << std::endl;
        closesocket(broadcast_socket);
        return false;
    } std::cout << "[ROVER] Broadcast message sent: " << message << std::endl;
    closesocket(broadcast_socket);
    return true;
} bool Rover::startListening() {
    if (running) {
        return true;
    } listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket == INVALID_SOCKET) {
        std::cerr << "[ROVER ERROR] Failed to create listen socket: " << WSAGetLastError() << std::endl;
        return false;
    }
    if (bind(listen_socket, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) == SOCKET_ERROR) {
        std::cerr << "[ROVER ERROR] Failed to bind listen socket: " << WSAGetLastError() << std::endl;
        closesocket(listen_socket);
        return false;
    }
    running = true;
    std::cout << "[ROVER] Listening for connections on port " << listen_port << std::endl;
    return true;
}
void Rover::stopListening() {
    if (running) {
        closesocket(listen_socket);
        running = false;
    }
}