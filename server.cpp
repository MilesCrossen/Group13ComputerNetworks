#include <iostream>
#include <cstring> // C-style strings
#include <winsock2.h>// Windows-specific socket library
#include <ws2tcpip.h> // TCP/IP utilities add-on
#include <ctime> // For timestamps + seeding
#include <cstdlib> // For RNG
#include <thread> // For parallel connection

#pragma comment(lib, "ws2_32.lib") //Link Winsock library for networking

#define PORT_ROCK 12345 // Each port handles a particulra request
#define PORT_TEMP 12346
#define PORT_MONOLITH 12347
#define PORT_RADIATION 12348
#define BUFFER_SIZE 256 // Data buffer size -> 256 bytes

void handleRequest(int port) { // Handles requests on a specific port, runs on its own thread
    SOCKET server_fd; // Socket instance
    struct sockaddr_in server_addr{}, client_addr{}; // Structs for storing addresses
    char buffer[BUFFER_SIZE]; // Buffer for incoming data
    int client_len = sizeof(client_addr); // Storing size of client address struct

    server_fd = socket(AF_INET, SOCK_DGRAM, 0);// Make UDP socket
    if (server_fd == INVALID_SOCKET) { // Just for resiliency in case of failure...
        std::cerr << "[ERROR] Socket creation failed on port " << port << ": " << WSAGetLastError() << std::endl;
        return;
    }

    server_addr.sin_family = AF_INET; // AF_INET means address family - internet i.e. we work w/IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // We listen for connections on all available interfaces
    server_addr.sin_port = htons(port); //Switching port number -> network byte order (htons means host to network short).
    //most network protoocls store numbers big endian (msb first)

    if(bind(server_fd,(struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Bind failed on port " << port << ": " << WSAGetLastError() << std::endl; // binds socket
        closesocket(server_fd); //Cleanup...
        return;
    }
    std::cout << "[INFO] Listening on port " << port << "...\n"; // Let the user know we're listening...
    int expectedSeqNum = 0; // RDT 3.0 sequence tracker(per threadb asis)
    std::string lastResponse = "";// Used if last good packet must be sent...

    while (true) { // This loop keeps running forever, listnes for new requests
        int bytes_received = recvfrom(server_fd, buffer, BUFFER_SIZE -1, 0,
                                      (struct sockaddr*)&client_addr, &client_len);
        if (bytes_received > 0){ // Check if we actually got data
            buffer[bytes_received] = '\0'; // Just for terminating rec string
            std::time_t now = std::time(nullptr); // Current time
            std::string received(buffer);
            std::cout<< "[Received @ " << std::ctime(&now) << "] " << received << std::endl;

            if (received.rfind("SEQ " + std::to_string(expectedSeqNum), 0) == 0) { // Only accept expected seq num
                std::string response; // The response we will send back
                if (port == PORT_ROCK) {
                    std::string rock_types[] = {"Basalt", "Regolith", "Anorthosite", "Breccia"};
                    response = "ACK " + std::to_string(expectedSeqNum) + " | ROCK_TYPE: " + rock_types[std::rand() % 4];
                }
                else if (port == PORT_TEMP) {
                    int temperature = (std::rand() % 121) - 50;
                    response = "ACK " + std::to_string(expectedSeqNum) + " | TEMP: " + std::to_string(temperature) + " C";
                }
                else if (port == PORT_MONOLITH) {
                    response = (std::rand() % 10 == 0) ? "ACK " + std::to_string(expectedSeqNum) + " | MONOLITH: YES" : "ACK " + std::to_string(expectedSeqNum) + " | MONOLITH: NO";
                }
                else if (port == PORT_RADIATION) {
                    float radiation = static_cast<float>(rand() % 250 + 10) / 100.0;
                    response = "ACK " + std::to_string(expectedSeqNum) + " | RADIATION: " + std::to_string(radiation) +" mSv";
                }

                lastResponse = response; // Save in case resend is needed
                sendto(server_fd, response.c_str(),response.length(), 0,
                       (struct sockaddr*)&client_addr, client_len); // Send the response back to the client
                std::cout << "[Sent] " << response << std::endl; // Log the response
                expectedSeqNum = 1 - expectedSeqNum; // Flip expected seq num
            } else {
                std::cout << "[DUPLICATE/OUT-OF-ORDER] Resending last response: " << lastResponse << std::endl; // Just resend
                sendto(server_fd, lastResponse.c_str(), lastResponse.length(), 0,
                       (struct sockaddr*)&client_addr, client_len);
            }
        }
    }
    closesocket(server_fd); // Close b4 exiting
}

int main() {
    WSADATA wsaData; // This holds Winsock startup data
    int wsaerr = WSAStartup(MAKEWORD(2, 2), &wsaData); // Initialise Winsock v2.2
    if (wsaerr != 0) { // Iffailure, print error and exit
        std::cerr << "WSAStartup failed: " << wsaerr << std::endl;
        return 1; // 1 = error usually
    }
    std::srand(std::time(nullptr)); // Seed the random number generator for realistic randomness, but in reality pseudorandom
    std::thread rockThread(handleRequest, PORT_ROCK); //Create a thread to handle rock data requests
    std::thread tempThread(handleRequest, PORT_TEMP); // Create a thread to handle temperature requests
    std::thread monolithThread(handleRequest, PORT_MONOLITH); // Create a thread for monolith requests
    std::thread radiationThread(handleRequest, PORT_RADIATION); // Create a thread for radiation data

    rockThread.join(); // Wait for the rock thread to finish
    tempThread.join(); // Same thing for temperature
    monolithThread.join(); // And monoliths...
    radiationThread.join(); // And radiation

    WSACleanup(); //Clean-up Winsock before exiting
    return 0; // Exit... and we are done
}