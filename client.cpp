#include <iostream>
#include <cstring>
#include <winsock2.h> // winsock2 is used for socket programming
#include <ws2tcpip.h> // winsock2 extension for tcp/ip protocols
#include <cstdlib> // Gens random numbers
#include <ctime> // srand() is used to generate a seed for randomisation and time()    // For srand() and time()
#include <thread> // Multi-threading
#include <chrono> // For sleeping

#pragma comment(lib, "ws2_32.lib") //For connecting winsock2

#define PORT 12345 // random port that doesn't interact
#define SERVER_IP "127.0.0.1" // Loopback address
#define BUFFER_SIZE 256
SOCKET sock_fd; //create a socket object
struct sockaddr_in server_addr; // create sock address object

void requestData() { // This function is used for random reqs
    while(true) { // i.e. always
        std::string input;
        std::getline(std::cin, input); // Wait for user input

        if (input == "rock") { // If user types 'rock' it requests a random rock type
            std::string request = "REQ ROCK_TYPE";
            sendto(sock_fd, request.c_str(), request.length(), 0, // We send req to server
                   (struct sockaddr*)&server_addr, sizeof(server_addr));
            std::cout << "[Manual Request] Sent: " << request << std::endl; // Just noting in output...

            // Block below is waiting for response
            char buffer[BUFFER_SIZE]; // up to [BUFFER SIZE] (256 chars)
            int server_len = sizeof(server_addr); // Just finding length of
            int bytes_received = recvfrom(sock_fd, buffer, BUFFER_SIZE - 1, 0, // receiving from server
                                          (struct sockaddr*)&server_addr, &server_len); //via socket
            if(bytes_received > 0) { // If data was received
                buffer[bytes_received] = '\0'; //null terminate received string so that it's a valid cstring
                std::cout << "[Server Response] " << buffer << std::endl;
            } else {
                std::cerr << "[ERROR] No response received from server." << std::endl;
            }
        } else if (input == "random") { // If user types 'random', send random telemetry data
            int random_number = (std::rand() % 100) + 1; // Generate random telemetry data
            std::string message = "Telemetry: " + std::to_string(random_number);
            sendto(sock_fd, message.c_str(), message.length(), 0,
                   (struct sockaddr*)&server_addr, sizeof(server_addr));
            std::cout << "Random telemetry data sent: " << message << std::endl;
        }
    }
}

int main() {
    WSADATA wsaData; // Initialise winsock WSADATA struct
    int wsaerr = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaerr != 0) { // Check for intiailisation errors
        std::cerr << "WSAStartup failed: " << wsaerr << std::endl;
        return 1; // EXit if failure
    }
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0); // We make ipv4 (AF_INET) UDP socket
    if (sock_fd == INVALID_SOCKET) { // Just checcking for failure
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();// Cleaning up resources
        return 1;
    }

    // Define server address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET,SERVER_IP, &server_addr.sin_addr)<= 0) {
        std::cerr << "Invalid address/Address not supported" << std::endl; // Just ensuring resiliency
        closesocket(sock_fd); // close
        WSACleanup(); // Clean up resources left over
        return 1;
    }

    std::cout << "Rover ready to send telemetry data and request rock types!" << std::endl;
    std::cout << "Type 'rock' to request moon rock type." << std::endl;
    std::cout << "Type 'random' to send random telemetry data." << std::endl;
    std::thread inputThread(requestData);
    inputThread.join(); // Ensuring main doesn't exit immediately

    // Cleaning up + closing socket
    closesocket(sock_fd);
    WSACleanup();
    return 0;
}
