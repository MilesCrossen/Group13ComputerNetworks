#include <iostream> // Standard input-output stream
#include <cstring> // C-style string functions
#include <winsock2.h>// Windows-specific socket programming
#include <ws2tcpip.h> // Additional TCP/IP utilities
#include <thread>// Multi-threading support
#pragma comment(lib, "ws2_32.lib") // Link Winsock library for networking
#define RELAY_IP "127.0.0.1" // Server IP
#define PORT_ROCK 5000 // Port spammm
#define PORT_TEMP 5001
#define PORT_MONOLITH 5002
#define PORT_RADIATION 5003
#define BUFFER_SIZE 256 // This is all explained in other files tbh idk why I'd explain it again
#define MAX_RETRIES 5 // Retry count for lost packets

int currentSeqNum = 0; // RDT 3.0 sequence tracker (flips between 0 and 1)

void requestData(int port, std::string requestMessage) { // Send req to relay
    SOCKET sock_fd;
    struct sockaddr_in relay_addr; // For relay address
    char buffer[BUFFER_SIZE]; // Incoming data..
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0); // UDP sock
    if (sock_fd == INVALID_SOCKET) {
        std::cerr << "[ERROR] Socket creation failed on port " << port << ": " << WSAGetLastError() << std::endl;
        return;
    }

    int timeout = 2000; // 2 second receive timeout
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    relay_addr.sin_family = AF_INET; // IPv4
    relay_addr.sin_port = htons(port); // Convert port to network byte order
    if (inet_pton(AF_INET, RELAY_IP, &relay_addr.sin_addr) <= 0) {
        std::cerr << "[ERROR] Invalid address for relay\n"; // check if IP address valid
        closesocket(sock_fd);
        return;
    }

    std::string packet = "SEQ " + std::to_string(currentSeqNum) + " | " + requestMessage; // Append seq num to msg
    int retries = 0; // How many times we tried
    bool acked = false; // Have we gotten the right ACK?

    while (retries < MAX_RETRIES && !acked) { // Stop-and-wait loop
        sendto(sock_fd, packet.c_str(), packet.length(), 0, // Send the packet
               (struct sockaddr*)&relay_addr, sizeof(relay_addr));
        std::cout << "[Sent] " << packet << " on port " << port << std::endl; // Log it

        int relay_len = sizeof(relay_addr); // Length of address
        int bytes_received = recvfrom(sock_fd, buffer, BUFFER_SIZE - 1, 0,  (struct sockaddr*)&relay_addr, &relay_len);

        if (bytes_received > 0) { // If we received a response
            buffer[bytes_received] = '\0'; //Terminate...
            std::string response(buffer);

            // Check for correct ACK
            if (response.rfind("ACK " + std::to_string(currentSeqNum), 0) == 0) {
                std::cout << "[Server Response via Relay] " << response << std::endl;
                acked = true;
                currentSeqNum = 1 - currentSeqNum; // Flip sequence number
            } else {
                std::cout << "[IGNORED] Unexpected ACK or corrupted packet: " << response << std::endl; // Ignore
            }
        } else {
            std::cout << "[TIMEOUT] No ACK received, retrying... (Attempt " << retries + 1 << ")\n"; // Timeout msg
            retries++;
        }
    }

    if (!acked) std::cerr << "[ERROR] Max retries reached. No valid response from server.\n"; // Max retries fail

    closesocket(sock_fd); // Close socket once done
}

int main() {
    WSADATA wsaData;
    int wsaerr = WSAStartup(MAKEWORD(2, 2), &wsaData); // More winsock spamming
    if (wsaerr != 0) { //If error
        std::cerr << "WSAStartup failed: " << wsaerr << std::endl;
        return 1;
    }
    std::cout << "Rover ready to send telemetry data via relay. Available commands:\n";
    std::cout << "'rock' -> Request rock type\n";
    std::cout << "'temp' -> Request temperature\n";
    std::cout << "'monolith' -> Request monolith presence\n";
    std::cout << "'radiation' -> Request radiation levels\n";
    while (true) { // Listen
        std::string input;
        std::getline(std::cin, input); // and read...

        if (input == "rock") requestData(PORT_ROCK, "REQ ROCK_TYPE"); // Rock type
        else if (input == "temp") requestData(PORT_TEMP, "REQ TEMP"); // Request temperature
        else if (input == "monolith") requestData(PORT_MONOLITH, "REQ MONOLITH");// Check for monolith presence
        else if (input == "radiation") requestData(PORT_RADIATION, "REQ RADIATION"); // Get radiation level
        else std::cout << "[ERROR] Unknown command\n"; // If not recognised, show error
    }

    WSACleanup(); // Cleanup
    return 0;
}
