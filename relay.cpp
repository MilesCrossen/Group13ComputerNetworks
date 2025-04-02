//RELAY.CPP
// WE RECOMMEND READING THE COMMENTS IN CLIENT.CPP AS A GOOD INTRO TO HOW OUR CODE WORKS
//AND SOME FUNCTIONS WE USE A LOT. THANKS

#include <iostream>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <vector>
#pragma comment(lib, "ws2_32.lib")
#define RELAY_PORT_ROCK 5000 // 500x are relay->server ports
#define RELAY_PORT_TEMP 5001
#define RELAY_PORT_MONOLITH 5002
#define RELAY_PORT_RADIATION 5003
#define RELAY_PORT_IMAGE 5004
#define SERVER_IP "127.0.0.1" // SERVER IP
#define BUFFER_SIZE 256
#define IMAGE_BUFFER_SIZE 2048
#define PACKET_DROP_RATE 0 // adjustable loss rate
#define PACKET_DELAY_MS 10 // Delay in ms
int timePassRate = 0; // How many mins pass per IRL second?
int current_time = 0; // Simulated time in minutes, increments quicker than IRL

//JUST FOR CLARIFICATION, PORTS 500x RECEIVE REQUESTS FROM THE CLIENT, PORTS 1234x-12350 COMMUNICATE W/SERVER


bool isCommunicationWindowOpen() {
    int cycle_time = current_time %(12 *60 + 50 + 12 * 60 + 50); // Full cycle = 25h 40m
    return cycle_time< (12 * 60 + 50); // First 12h 50m = open for communication, next 12h 50 -> closed
}
void updateTime() {
    while(true) { // (always)
        std::this_thread::sleep_for(std::chrono::seconds(1));
        current_time += timePassRate; // The value in here is how many simulated mins we accelerate through
        //per real-life second. figures out whether there is a signal blockage
    }
}

//BELOW MOSTLY FOR FORWARDING TO SERVER

void relayImageData(int relayPort, int serverPort){ // For relaying image data between earth and rover
    SOCKET relay_socket;
    struct sockaddr_in relay_addr{}, server_addr{}, client_addr{}; // These store addresses for each stage
    char buffer[IMAGE_BUFFER_SIZE]; // Big buffer for image data bcs it's at a much larger scale
    int client_len = sizeof(client_addr),server_len= sizeof(server_addr); // Lengths needed for recvfrom if you remember from client.cpp

    relay_socket = socket(AF_INET, SOCK_DGRAM, 0); // Making UDP socket...

    relay_addr.sin_family = AF_INET; //AF_INET -> IPv4
    relay_addr.sin_addr.s_addr = INADDR_ANY; // INADDR_ANY means listen on all interfaces pretty much
    relay_addr.sin_port = htons(relayPort);
    if (bind(relay_socket, (struct sockaddr*)&relay_addr, sizeof(relay_addr)) == SOCKET_ERROR) { // Bind socket to port
        std::cerr << "[ERROR] Bind failed on image relay port " << relayPort << ": " << WSAGetLastError() << std::endl;
        closesocket(relay_socket); // Cleanup if failure
        return;
    }
    std::cout << "[INFO] Image relay listening on port " << relayPort << "...\n";
    while (true){ // Wiating for image request
        int bytes_received = recvfrom(relay_socket, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr*)&client_addr, &client_len);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            if(isCommunicationWindowOpen()) {
                std::cout << "[CLIENT -> RELAY] " << buffer << " (Port "<< relayPort << ") (Time: " << current_time << " min)\n";
                server_addr.sin_family = AF_INET; //<- in this block we are setting up server port,
                server_addr.sin_port = htons(serverPort); // htons converts a port number to
                //whatever byte order is used locally (big endian/little endian)
                inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr); // Convert string IP to binary
                // Similar to htons, converts ip address in human readable format to binary (i.e. loopback address to binary).
                std::this_thread::sleep_for(std::chrono::milliseconds(PACKET_DELAY_MS));
                float randomValue = static_cast<float>(rand()) / RAND_MAX; // Add some delay + potential packet loss
                if (randomValue < PACKET_DROP_RATE) {
                    std::cout << "[DROPPED] Image request dropped before reaching server (Port " << relayPort << ", Time: " << current_time << " min)\n";
                    continue; // Skip packet
                }
                sendto(relay_socket, buffer, bytes_received, 0, (struct sockaddr*)&server_addr, sizeof(server_addr)); // Forward to server

                bytes_received = recvfrom(relay_socket, buffer, IMAGE_BUFFER_SIZE - 1, 0, (struct sockaddr*)&server_addr, &server_len);
                //Aand receiving from server now
                if (bytes_received > 0){
                    buffer[bytes_received] = '\0';
                    std::string response(buffer);
                    if (response.find("IMAGE_SIZE:") != std::string::npos) { // Finds first appearance of IMAGE SIZE: inside response
                        std::cout << "[SERVER -> RELAY] " << response << " (Port " << serverPort << ") (Time: " << current_time << " min)\n";
                        std::this_thread::sleep_for(std::chrono::milliseconds(PACKET_DELAY_MS));
                        //Thing to note here for documentation/prof: We delay packets in BOTH directions
                        //by PACKET_DELAY_MS so our PACKET_DELAY_MS should be half of earth-moon RTT

                        randomValue = static_cast<float>(rand()) / RAND_MAX;// rand number
                        if (randomValue < PACKET_DROP_RATE) { // More packet loss :( (packet loss in both directions too)
                            std::cout << "[DROPPED] Image size dropped before reaching client (Port " << relayPort << ", Time: " << current_time << " min)\n";
                            continue; // Skip if dropped
                        }
                        sendto(relay_socket, buffer, bytes_received, 0, (struct sockaddr*)&client_addr, client_len);
                        bytes_received = recvfrom(relay_socket, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr*)&client_addr, &client_len); // wait for client ack

                        if (bytes_received > 0) { // Got ack from client!!!
                            sendto(relay_socket, buffer, bytes_received, 0, (struct sockaddr*)&server_addr, server_len); // Now send->server!
                            bool transfer_complete = false; // Useful bcs transfer usually occurs in many steps unless image is tiny
                            while (!transfer_complete){
                                bytes_received = recvfrom(relay_socket, buffer, IMAGE_BUFFER_SIZE - 1, 0, (struct sockaddr*)&server_addr, &server_len); // Next chunk from server

                                if (bytes_received > 0) {
                                    if (bytes_received < 20) { // Small message = control signal (like ack, seq etc)
                                        buffer[bytes_received] = '\0';
                                        std::string msg(buffer);
                                        if (msg =="IMAGE_COMPLETE") {
                                            std::cout << "[SERVER -> RELAY] Image transfer complete (Port " << serverPort << ") (Time: " << current_time << " min)\n";
                                            sendto(relay_socket, buffer, bytes_received, 0, (struct sockaddr*)&client_addr, client_len); // Tell client we're donen ow
                                            transfer_complete = true;
                                            break;
                                        }
                                    }
                                    std::string header_str(buffer, std::min(20, bytes_received)); // Similar to client, makes string from first (up to 20) bytes of buffer
                                    // Above line captures header info of packet, which isn't payload
                                    if(header_str.find("CHUNK ") == 0) { // is chunk there?
                                        size_t header_end = 0;
                                        for (size_t i = 6; i < bytes_received && i < 30; i++) { // index of chunk->6
                                            if (buffer[i] == ' ') {
                                                header_end = i;
                                                break; // So if it's a chunk, we look for the end of the chunk
                                                //Starts at position 6, looks for next space char which is end of chunk info
                                            }
                                        }

                                        if (header_end > 0) { //space
                                            std::string chunk_info = header_str.substr(6, header_end - 6);//We take 6 away because remember, it started @ 6
                                            std::cout << "[SERVER -> RELAY] Image chunk " << chunk_info << " (Port " << serverPort << ") (Time: " << current_time << " min)\n"; // Printed msg for tracking chunk updates in image
                                        }
                                    }
                                    // Just to clarify, we start @ pos 6 because we send 'CHUNK ' as a prefix for image chunks
                                    //so that's why we start at position 6...

                                    //BELOW IS FOR FORWARDING TO CLIENT
                                    std::this_thread::sleep_for(std::chrono::milliseconds(PACKET_DELAY_MS)); // This block is for
                                    randomValue = static_cast<float>(rand()) /RAND_MAX; //forwarding chunk to client
                                    if (randomValue < PACKET_DROP_RATE) { // Simulate loss
                                        std::cout << "[DROPPED] Image chunk dropped before reaching client (Port " << relayPort << ", Time: " << current_time << " min)\n";
                                        continue; // if dropped then skip
                                    }
                                    sendto(relay_socket, buffer, bytes_received, 0, (struct sockaddr*)&client_addr, client_len);
                                    // Wait for client to ack chunk
                                    bytes_received = recvfrom(relay_socket, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr*)&client_addr, &client_len);
                                    if (bytes_received > 0) { // This bit of code forwards back to server
                                        sendto(relay_socket, buffer, bytes_received, 0,
                                               (struct sockaddr*)&server_addr, server_len);
                                    }
                                }
                            }
                        }
                    }
                }
            }else {
                std::cout << "[BLOCKED] Image transmission rejected. Satellite out of range. (Time: " << current_time << " min)\n";
                std::string response = "BLOCKED: Satellite out of range"; // Error msg if satellite timing doesnt match
                sendto(relay_socket, response.c_str(), response.length(), 0, (struct sockaddr*)&client_addr, client_len);
            }
        }
    }
}

//THIS IS FOR RELAYING DATA INSTEAD OF IMAGES. WE SEPARATE OUR RELAYING OF IMAGES VS DATA
void relayData(int relayPort, int serverPort) {
    SOCKET relay_socket; // All the same we've done already
    struct sockaddr_in relay_addr{}, server_addr{}, client_addr{};
    char buffer[BUFFER_SIZE];
    int client_len = sizeof(client_addr), server_len = sizeof(server_addr);
    relay_socket = socket(AF_INET, SOCK_DGRAM, 0);
    relay_addr.sin_family = AF_INET; // IPv4
    relay_addr.sin_addr.s_addr = INADDR_ANY; // This is all the same stuff we've done already multiple times
    relay_addr.sin_port = htons(relayPort);

    if (bind(relay_socket,(struct sockaddr*)&relay_addr, sizeof(relay_addr)) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Bind failed on relay port " << relayPort << ": " << WSAGetLastError() << std::endl;
        closesocket(relay_socket); // Close
        return;
    } // Just for binding like we've done already

    std::cout << "[INFO] Relay listening on port " << relayPort << "...\n";
    while(true) { // Keep listening always...
        int bytes_received = recvfrom(relay_socket, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr*)&client_addr, &client_len);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            if(isCommunicationWindowOpen()) {
                std::cout << "[ROVER -> RELAY] " <<buffer <<  " (Port " << relayPort << ") (Time: " << current_time << " min)\n";
                server_addr.sin_family = AF_INET; // Forwarding to serv
                server_addr.sin_port = htons(serverPort); // More conversions...
                inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
                std::this_thread::sleep_for(std::chrono::milliseconds(PACKET_DELAY_MS)); // Delay like we've done already
                float randomValue = static_cast<float>(rand()) / RAND_MAX; // Just to be clear, this gens a float between 0 and 1 and we test if it's below our drop rate
                if (randomValue < PACKET_DROP_RATE) {
                    std::cout << "[DROPPED] Packet dropped before reaching server (Port " << relayPort << ", Time: " << current_time << " min)\n";
                    goto loop_continue;
                }
                sendto(relay_socket, buffer, bytes_received, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
// Send to server
                bytes_received = recvfrom(relay_socket, buffer, BUFFER_SIZE - 1, 0,(struct sockaddr*)&server_addr, &server_len); // Wait for answer from server
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    std::cout << "[SERVER -> RELAY] " << buffer << " (Port " << serverPort << ") (Time: " << current_time << " min)\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(PACKET_DELAY_MS)); // Simulate delay
                    randomValue = static_cast<float>(rand()) / RAND_MAX; //2-way packet loss/delay
                    if (randomValue < PACKET_DROP_RATE) {
                        std::cout << "[DROPPED] Response dropped before reaching client (Port " << relayPort << ", Time: " << current_time << " min)\n";
                        goto loop_continue;
                    }
                    sendto(relay_socket, buffer, bytes_received, 0,(struct sockaddr*)&client_addr, client_len); // Return msg to client
                }
            }else{
                std::cout << "[BLOCKED] Transmission rejected. Satellite out of range. (Time: " << current_time << " min)\n"; // if relay out of range, reject
                //transmission
                std::string response = "BLOCKED: Satellite out of range"; // Sending blocked msg
                sendto(relay_socket, response.c_str(), response.length(), 0, (struct sockaddr*)&client_addr, client_len);
            }
        }
        loop_continue:
    }
}

int main() {
    WSADATA wsaData;
    int wsaerr = WSAStartup(MAKEWORD(2, 2), &wsaData); // Initialise winsock
    std::thread timeThread(updateTime); // Thread for time... used for determining if communication is open or not
    std::thread rockRelay(relayData, RELAY_PORT_ROCK, 12345); // Relay threads for each data type
    std::thread tempRelay(relayData, RELAY_PORT_TEMP, 12346);
    std::thread monolithRelay(relayData, RELAY_PORT_MONOLITH, 12347);
    std::thread radiationRelay(relayData, RELAY_PORT_RADIATION, 12348);
    std::thread imageRelay(relayImageData, RELAY_PORT_IMAGE, 12349); // New thread for image relay

    timeThread.detach(); // detach means its run in the background i.e. non blocking
    rockRelay.join(); // join means we wait for the threads to finish, but they dont bcs theyre listening infinitely
    tempRelay.join();
    monolithRelay.join();
    radiationRelay.join();
    imageRelay.join();

    WSACleanup();
    return 0;// Then... clean + exit
}