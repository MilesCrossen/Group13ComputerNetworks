#include <iostream>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ctime> // For timestamps + srand seeding
#include <cstdlib> // Rand rock type

#pragma comment(lib, "ws2_32.lib")

#define PORT 12345
#define BUFFER_SIZE 256

int main() {
    WSADATA wsaData; // Initalising winsock + struct for winsock implementation
    int wsaerr = WSAStartup(MAKEWORD(2, 2), &wsaData); // Vers 2.2
    if (wsaerr != 0) { // Checks for failure
        std::cerr << "WSAStartup failed: " << wsaerr << std::endl;
        return 1;
    }
    SOCKET server_fd = socket(AF_INET, SOCK_DGRAM, 0); //UDP socket creation
    if (server_fd == INVALID_SOCKET) {// Just error creation checking
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup(); // Clean up resources
        return 1;
    }



    struct sockaddr_in server_addr{}, client_addr{};// Structs for server + client addresses
    server_addr.sin_family = AF_INET;//ipv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // We accept connections on any  IP
    server_addr.sin_port = htons(PORT); // Changing port no. to network byte order



    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        //Binding socket to port
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl; // More boring error checking...
        closesocket(server_fd); // Close socket if error occurred
        WSACleanup();
        return 1;
    }


    std::cout << "Waiting for rover telemetry data and requests..." << std::endl;
    char buffer[BUFFER_SIZE]; // buffer of 256 chars or whatever amount requested
    int client_len = sizeof(client_addr);// Size of client address struct

    std::srand(std::time(nullptr)); // Seeding for rock type randomness

    while (true) {// always true
        int bytes_received = recvfrom(server_fd, buffer, BUFFER_SIZE - 1, 0,
                                      (struct sockaddr*)&client_addr, &client_len);
        if (bytes_received > 0) { // Check if data received w/o error
            buffer[bytes_received] = '\0';// Just a null termination
            std::time_t now = std::time(nullptr); //Cur timestamp
            std::cout << "[Received @ " << std::ctime(&now) << "] " << buffer << std::endl;
            // just printing time
            if(strcmp(buffer, "REQ ROCK_TYPE") == 0) { // Check if rock type. When we add more
                //parameters we should use switch cases for cleaner coding
                std::string rock_types[] = {"Basalt", "Regolith", "Anorthosite", "Breccia"};
                // Cool rock types!!
                std::string response = "ROCK_TYPE: " + rock_types[std::rand() % 4]; // Rand. rock type from list

                sendto(server_fd, response.c_str(), response.length(), 0, // We reeturn msg
                       (struct sockaddr*)&client_addr, client_len); // to client.
                std::cout << "[Sent] " << response << std::endl; // Print response sent bck to client
            }
        }else{
            std::cerr << "Receive failed: " << WSAGetLastError() << std::endl; // If failure
            break;
        }
    }




    closesocket(server_fd); // Cleaning up + closing socket when exit occurs
    WSACleanup();
    return 0;
}