//RELAY.CPP

#include <iostream> // Standard input-output
#include <cstring> // String handling
#include <winsock2.h> // Windows-specific socket API
#include <ws2tcpip.h>// TCP/IP utility functions
#include <thread> // For handling multiple connections in parallel
#include <chrono> // For time management/updates
#include <cstdlib> // For rand()
#include <vector> // For storing image chunks

#pragma comment(lib, "ws2_32.lib") // Winsock library

#define RELAY_PORT_ROCK 5000 // Relay -> rover communication ports
#define RELAY_PORT_TEMP 5001
#define RELAY_PORT_MONOLITH 5002
#define RELAY_PORT_RADIATION 5003
#define RELAY_PORT_IMAGE 5004 // New port for image requests
#define SERVER_IP "127.0.0.1" // IP address of server
#define BUFFER_SIZE 256 // Max msg size
#define IMAGE_BUFFER_SIZE 2048 // Larger buffer for image chunks

#define PACKET_DROP_RATE 0 // adjustable loss rate
#define PACKET_DELAY_MS 10 // Delay in ms

int timePassRate = 0; // How many mins pass per IRL second?
int current_time = 0; // Simulated time in minutes, increments quicker than IRL

bool isCommunicationWindowOpen() { // Checks if we can communicate based on window
    int cycle_time = current_time %(12 *60 + 50 + 12 * 60 + 50); // Full cycle = 25h 40m
    //current_time is updated periodically in the updateTime() function, which runs on its own
    //thread to avoid interfering w/other thingies
    return cycle_time< (12 * 60 + 50); // First 12h 50m = open for communication, next 12h 50 -> closed
}

void updateTime() { // Updates time every real life second
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        current_time += timePassRate; // The value in here is how many simulated mins we accelerate through
        //per real-life second. Determines whether there is a signal blockage
    }
}

void relayImageData(int relayPort, int serverPort) { // For relaying image data between earth and rover
    SOCKET relay_socket; // Socket for satellite relay
    struct sockaddr_in relay_addr{}, server_addr{}, client_addr{}; // Address structs for all parties
    char buffer[IMAGE_BUFFER_SIZE]; // Big buffer for image data bcs it's at a much larger scale
    int client_len = sizeof(client_addr),server_len= sizeof(server_addr); // Needed for recvfrom

    relay_socket = socket(AF_INET, SOCK_DGRAM, 0); // Making UDP socket...
    if (relay_socket == INVALID_SOCKET) { // If it fails...
        std::cerr << "[ERROR] Socket creation failed on image relay port " << relayPort << ": " << WSAGetLastError() << std::endl;
        return;
    }



    relay_addr.sin_family = AF_INET; // IPv4 family
    relay_addr.sin_addr.s_addr = INADDR_ANY; // Listen on interfaces
    relay_addr.sin_port = htons(relayPort); // Port- > network byte order

    if (bind(relay_socket, (struct sockaddr*)&relay_addr, sizeof(relay_addr)) == SOCKET_ERROR) { // Bind socket to port
        std::cerr << "[ERROR] Bind failed on image relay port " << relayPort << ": " << WSAGetLastError() << std::endl;
        closesocket(relay_socket); // Cleanup if failure
        return;
    }


    std::cout << "[INFO] Image relay listening on port " << relayPort << "...\n"; // Ready to relay images...


    while (true){ // Infinite loop for continuous operation
        // Wait for client to request image... zzz...
        int bytes_received = recvfrom(relay_socket, buffer, BUFFER_SIZE - 1,
            0, (struct sockaddr*)&client_addr, &client_len);

        if (bytes_received > 0) { // Got sth from client
            buffer[bytes_received] = '\0'; // Null terminate

            if (isCommunicationWindowOpen()) { // Check if satellite in range
                std::cout << "[CLIENT -> RELAY] " << buffer << " (Port "<< relayPort << ") (Time: " << current_time << " min)\n";



                server_addr.sin_family = AF_INET; // IPv4 again <- in this block we are setting up server
                //address for forwarding btw
                server_addr.sin_port = htons(serverPort); // htons converts a port number to
                //whatever byte order is used locally (big endian/little endian)
                inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr); // Convert string IP to binary
                // Similar to htons, converts ip address in human readable format to binary (i.e. loopback address to binary)

                std::this_thread::sleep_for(std::chrono::milliseconds(PACKET_DELAY_MS)); // Here we add
                float randomValue = static_cast<float>(rand()) / RAND_MAX;//some delay
                if (randomValue < PACKET_DROP_RATE) { // aaand packetl oss
                    std::cout << "[DROPPED] Image request dropped before reaching server (Port " << relayPort << ", Time: " << current_time << " min)\n";
                    continue; // Skip packet
                }


                sendto(relay_socket, buffer, bytes_received, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
                // Forward to server


                bytes_received = recvfrom(relay_socket, buffer, IMAGE_BUFFER_SIZE - 1, 0, // Here we wait for response w/
                    (struct sockaddr*)&server_addr,
                    &server_len); //size too

                if (bytes_received > 0) { // Server responded
                    buffer[bytes_received] = '\0'; // Null terminate
                    std::string response(buffer); // Makes object (response) w/contents from buffer

                    if (response.find("IMAGE_SIZE:") != std::string::npos) { // Size info finding
                        std::cout << "[SERVER -> RELAY] " << response <<
                            " (Port " << serverPort << ") (Time: " << current_time << " min)\n";

                        std::this_thread::sleep_for(std::chrono::milliseconds(PACKET_DELAY_MS)); // More delay...
                        //Thing to note here for documentation/prof: We delay packets in BOTH directions
                        //by PACKET_DELAY_MS so our PACKET_DELAY_MS should be half of earth-moon RTT

                        randomValue = static_cast<float>(rand()) / RAND_MAX;// Sending info->client
                        if (randomValue < PACKET_DROP_RATE) { // More packet loss :( (packet loss in both directions too)
                            std::cout << "[DROPPED] Image size dropped before reaching client (Port " << relayPort << ", Time: " << current_time << " min)\n";
                            continue; // Skip if dropped
                        }

                        sendto(relay_socket, buffer, bytes_received, 0, (struct sockaddr*)&client_addr, client_len);

                        bytes_received = recvfrom(relay_socket, buffer, BUFFER_SIZE - 1, 0,
                                                 (struct sockaddr*)&client_addr, &client_len); // wait for client ack

                        if (bytes_received > 0) { // Got ack from client!!!
                            sendto(relay_socket, buffer, bytes_received, 0, (struct sockaddr*)&server_addr, server_len);
                            // Now send ->server

                            bool transfer_complete = false; // Useful bcs transfer usually occurs in many steps unless image is tiny
                            while (!transfer_complete){// Keep going until done
                                bytes_received = recvfrom(relay_socket, buffer, IMAGE_BUFFER_SIZE - 1, 0,
                                                         (struct sockaddr*)&server_addr, &server_len); // Next chunk from server

                                if (bytes_received > 0) { // Got data from server
                                    // If end msg?
                                    if (bytes_received < 20) { // Small message = control signal (like ack, seq etc)
                                        buffer[bytes_received] = '\0';
                                        std::string msg(buffer);
                                        if (msg == "IMAGE_COMPLETE") { // Transfer done
                                            std::cout << "[SERVER -> RELAY] Image transfer complete (Port " << serverPort
                                                      << ") (Time: " << current_time <<
                                                          " min)\n";

                                            sendto(relay_socket, buffer, bytes_received, 0, // Tell client we're done now
                                                   (struct sockaddr*)&client_addr, client_len);
                                            transfer_complete = true; // Transfer now done
                                            break;
                                        }
                                    }

                                    // Extract chunk info for logging
                                    std::string header_str(buffer, std::min(20, bytes_received));
                                    // Above line captures header info of packet, which isn't payload
                                    if(header_str.find("CHUNK ") == 0) { // Is it a chunk?
                                        size_t header_end = 0;
                                        for (size_t i = 6; i < bytes_received && i < 30; i++) {
                                            if (buffer[i] == ' ') {
                                                header_end = i;
                                                break; // So if it's a chunk, we look for the end of the chunk
                                                //Starts at position 6, looks for next space char which is end of chunk info
                                            }
                                        }

                                        if (header_end > 0) { // Found header separator (space)
                                            std::string chunk_info = header_str.substr(6, header_end - 6);
                                            //We take 6 away because remember, it started @ 6
                                            std::cout << "[SERVER -> RELAY] Image chunk " << chunk_info
                                                      << " (Port " << serverPort << ") (Time: "
                                            << current_time << " min)\n"; // Printed msg for tracking chunk updates in image
                                        }
                                    }
                                    // Just to clarify, we start @ pos 6 because we send 'CHUNK ' as a prefix for image chunks
                                    //so that's why we start at position 6...

                                    std::this_thread::sleep_for(std::chrono::milliseconds(PACKET_DELAY_MS)); // This block is for
                                    randomValue = static_cast<float>(rand()) /RAND_MAX; //forwarding chunk to client
                                    if (randomValue < PACKET_DROP_RATE) { // Simulate chunk loss
                                        std::cout << "[DROPPED] Image chunk dropped before reaching client (Port "
                                                  << relayPort << ", Time: " << current_time << " min)\n";
                                        continue; // if dropped then skip chunk
                                    }

                                    sendto(relay_socket, buffer, bytes_received, 0,
                                           (struct sockaddr*)&client_addr, client_len);

                                    // Wait for client to ack the chunk
                                    bytes_received = recvfrom(relay_socket, buffer, BUFFER_SIZE - 1, 0,
                                                             (struct sockaddr*)&client_addr,
                                                             &client_len);

                                    if (bytes_received > 0) { // Got client ack
                                        // Forward ack to server
                                        sendto(relay_socket, buffer, bytes_received, 0,
                                               (struct sockaddr*)&server_addr, server_len);
                                    }
                                }
                            }
                        }
                    }
                }
            } else { // Window closed now
                std::cout << "[BLOCKED] Image transmission rejected. Satellite out of range. (Time: " << current_time << " min)\n";
                std::string response = "BLOCKED: Satellite out of range"; // Error msg
                sendto(relay_socket, response.c_str(), response.length(), 0,
                       (struct sockaddr*)&client_addr,
                       client_len);
            }
        }
    }

    closesocket(relay_socket); // Cleanup for robustness but I don't think we actually get here ever
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
    std::thread imageRelay(relayImageData, RELAY_PORT_IMAGE, 12349); // New thread for image relay

    timeThread.detach(); // Time thread runs independently (doesn't block execution)
    rockRelay.join(); // Wait for the rock relay thread to finish (it never will)
    tempRelay.join(); // Wait for the temp relay thread
    monolithRelay.join(); // Wait for the monolith relay thread
    radiationRelay.join(); // Wait for the radiation relay thread
    imageRelay.join(); // Wait for the image relay thread

    WSACleanup();
    return 0;// Then... clean + exit
}