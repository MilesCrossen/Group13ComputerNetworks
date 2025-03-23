#include <iostream> // Standard input-output
#include <cstring> // String handling
#include <winsock2.h> // Windows-specific socket API
#include <ws2tcpip.h>// TCP/IP utility functions
#include <thread> // For handling multiple connections in parallel
#include <chrono> // Time management
#include <cstdlib> // For rand()

#pragma comment(lib, "ws2_32.lib") // Winsock library

#define RELAY_PORT_ROCK 5000 // Relay -> rover communication ports
#define RELAY_PORT_TEMP 5001
#define RELAY_PORT_MONOLITH 5002
#define RELAY_PORT_RADIATION 5003
#define SERVER_IP "127.0.0.1" // IP address of server
#define BUFFER_SIZE 256 // Max msg size

#define PACKET_DROP_RATE 0.1 // 20% drop rate
#define PACKET_DELAY_MS 0 // Delay in ms

int current_time = 0; // Simulated time in minutes, increments quicker than IRL

bool isCommunicationWindowOpen() { // checks if we can communicate based on window
    int cycle_time = current_time %(12 *60 + 50 + 12 * 60 + 50); // Full cycle = 25h 40m
    return cycle_time< (12 * 60 + 50); // First 12h 50m = open for communication, next 12h 50 -> closed
}

void updateTime() { // 50*60x speedup (50 mins pass every 1 second)
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        current_time += 0;
    }
}

void relayData(int relayPort, int serverPort) { // for communication between earth + rover
    SOCKET relay_socket; // mMake socket
    struct sockaddr_in relay_addr{}, server_addr{}, client_addr{}; // Structs
    char buffer[BUFFER_SIZE];
    int client_len = sizeof(client_addr), server_len = sizeof(server_addr);
    //Lengths of client + server address structs

    relay_socket = socket(AF_INET, SOCK_DGRAM, 0); // Create a UDP socket (SOCK_DGRAM = datagram socket)
    if (relay_socket == INVALID_SOCKET) { // If socket creation fails, print an error
        std::cerr << "[ERROR] Socket creation failed on relay port " << relayPort << ": " << WSAGetLastError() << std::endl;
        return;
    }

    relay_addr.sin_family = AF_INET; // IPv4
    relay_addr.sin_addr.s_addr = INADDR_ANY; // Listen...
    relay_addr.sin_port = htons(relayPort); // Convert port to network byte order using same logic described in server cpp

    if (bind(relay_socket,(struct sockaddr*)&relay_addr, sizeof(relay_addr)) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Bind failed on relay port " << relayPort << ": " << WSAGetLastError() << std::endl;// Binding socket to port
        closesocket(relay_socket); // Close
        return;
    }
    std::cout << "[INFO] Relay listening on port " << relayPort << "...\n"; // Relay is ready...
    while(true) { // Keep listening always...
        int bytes_received = recvfrom(relay_socket, buffer, BUFFER_SIZE - 1, 0,
        (struct sockaddr*)&client_addr, &client_len);
        if (bytes_received > 0) { // if data rec'd
            buffer[bytes_received] = '\0'; // Termination.

            if(isCommunicationWindowOpen()) { // Istransmission allowed???
                std::cout << "[ROVER -> RELAY] " <<buffer <<  " (Port " << relayPort << ") (Time: " << current_time << " min)\n";
                server_addr.sin_family = AF_INET; // Forwarding to serv
                server_addr.sin_port = htons(serverPort); // More conversions...
                inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

                std::this_thread::sleep_for(std::chrono::milliseconds(PACKET_DELAY_MS)); // Simulate delay
                float randomValue = static_cast<float>(rand()) / RAND_MAX;
                if (randomValue < PACKET_DROP_RATE) {
                    std::cout << "[DROPPED] Packet dropped before reaching server (Port " << relayPort << ", Time: " << current_time << " min)\n";
                    goto loop_continue;
                }

                sendto(relay_socket, buffer, bytes_received, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

                bytes_received = recvfrom(relay_socket, buffer, BUFFER_SIZE - 1, 0,(struct sockaddr*)&server_addr, &server_len); // Wait for answer
                if (bytes_received > 0) { // More basic checks...
                    buffer[bytes_received] = '\0';
                    std::cout << "[SERVER -> RELAY] " << buffer << " (Port " << serverPort << ") (Time: " << current_time << " min)\n";

                    std::this_thread::sleep_for(std::chrono::milliseconds(PACKET_DELAY_MS)); // Simulate delay
                    randomValue = static_cast<float>(rand()) / RAND_MAX;
                    if (randomValue < PACKET_DROP_RATE) {
                        std::cout << "[DROPPED] Response dropped before reaching client (Port " << relayPort << ", Time: " << current_time << " min)\n";
                        goto loop_continue;
                    }

                    sendto(relay_socket, buffer, bytes_received, 0,(struct sockaddr*)&client_addr, client_len); // Return msg
                }
            }else{
                std::cout << "[BLOCKED] Transmission rejected. Satellite out of range. (Time: " << current_time << " min)\n"; // if relay out of range, reject
                //transmission

                std::string response = "BLOCKED: Satellite out of range"; // Sending blocked msg
                sendto(relay_socket, response.c_str(), response.length(), 0,
                       (struct sockaddr*)&client_addr, client_len);
            }
        }

        loop_continue:
        continue;
    }

    closesocket(relay_socket);
}

int main() {
    WSADATA wsaData;
    int wsaerr = WSAStartup(MAKEWORD(2, 2), &wsaData); // Initialise winsock
    if (wsaerr != 0) {// Just error logic
        std::cerr << "WSAStartup failed: " << wsaerr << std::endl;
        return 1;
    }

    std::thread timeThread(updateTime); // Thread for time...

    std::thread rockRelay(relayData, RELAY_PORT_ROCK, 12345); // Relay threads for each data type
    std::thread tempRelay(relayData, RELAY_PORT_TEMP, 12346);
    std::thread monolithRelay(relayData, RELAY_PORT_MONOLITH, 12347);
    std::thread radiationRelay(relayData, RELAY_PORT_RADIATION, 12348);

    timeThread.detach(); // Time thread runs independently (doesn't block execution)
    rockRelay.join(); // Wait for the rock relay thread to finish (it never will)
    tempRelay.join(); // Wait for the temp relay thread
    monolithRelay.join(); // Wait for the monolith relay thread
    radiationRelay.join(); // Wait for the radiation relay thread

    WSACleanup();
    return 0;// Then... clean + exit
}