//SERVER.CPP

#include <iostream>
#include <cstring> // C-style strings
#include <winsock2.h>// Windows-specific socket library
#include <ws2tcpip.h> // TCP/IP utilities add-on
#include <ctime> // For timestamps + seeding
#include <cstdlib> // For RNG
#include <thread> // For parallel connection
#include <fstream> // For reading image files
#include <vector> // For storing image data
#include <map> // For tracking chunks

#pragma comment(lib, "ws2_32.lib") //Link Winsock library for networking

#define PORT_ROCK 12345 // Each port handles a particulra request
#define PORT_TEMP 12346
#define PORT_MONOLITH 12347
#define PORT_RADIATION 12348
#define PORT_IMAGE 12349 // New port for image requests
#define BUFFER_SIZE 256 // Data buffer-> 256 bytes
#define CHUNK_SIZE 512 // Size for image chunks
#define IMAGE_MAX_RETRIES 5 // Max retries for image chunks

void handleImageRequest(SOCKET server_fd, struct sockaddr_in client_addr, int client_len, int seq_num) {
    // Path to the image file
    std::string image_path = "moon_image.jpg"; // Or whatever image path, we will randomise this later on

    std::ifstream file(image_path, std::ios::binary); // Read image file
    if (!file) { // If not existing
        std::string error_msg = "ACK " + std::to_string(seq_num) + " | IMAGE: ERROR_LOADING"; // Error
        sendto(server_fd, error_msg.c_str(), error_msg.length(), 0,
               (struct sockaddr*)&client_addr, client_len);
        std::cerr << "[ERROR] Could not open image file: " << image_path << std::endl;
        return;
    }

    file.seekg(0, std::ios::end); // Getting file size
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> image_data(file_size); // + read into buffer
    file.read(image_data.data(), file_size);
    file.close();

    std::string size_msg = "ACK " + std::to_string(seq_num) + " | IMAGE_SIZE: " + std::to_string(file_size);
    // Send file size first

    bool size_acked = false; // not acked yet
    int size_retries = 0;
    char buffer[BUFFER_SIZE];

    while(!size_acked && size_retries < IMAGE_MAX_RETRIES) { // While not ACKed
        sendto(server_fd, size_msg.c_str(), size_msg.length(), 0,
               (struct sockaddr*)&client_addr, client_len);
        std::cout << "[Sent] " << size_msg << std::endl;

        fd_set readfds; // Setup timeout for ack using select
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        tv.tv_sec = 2; //2 seconds
        tv.tv_usec = 0;

        if(select(server_fd + 1,&readfds, NULL, NULL, &tv) > 0) { //
            int bytes_received = recvfrom(server_fd, buffer, BUFFER_SIZE - 1, 0,
                                         (struct sockaddr*)&client_addr, &client_len);
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                std::string ack(buffer);
                if (ack == "ACK_SIZE") {
                    size_acked = true;
                    std::cout << "[Received] Size acknowledgment from client" << std::endl;
                }
            }
        } else {
            size_retries++;
            std::cout << "[TIMEOUT] No size acknowledgment, retrying... (Attempt " << size_retries << ")\n";
        }
    }

    if (!size_acked) {
        std::cerr << "[ERROR] Failed to send image size after " << IMAGE_MAX_RETRIES << " attempts" << std::endl;
        return;
    }

    int total_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;  // Ceiling division to calc no. chunks needed
    std::map<int, bool> chunk_acked;
    for (int i = 0; i < total_chunks; i++) { // Tracking ACked chunks
        chunk_acked[i] = false;
    }

    bool all_chunks_acked = false; //Keep sending until all chunks ACKed

    while (!all_chunks_acked) {
        all_chunks_acked = true; // Assume all are acked unless we find otherwise

        fd_set readfds; // Check for retry requests or send next unacked chunk
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;

        if (select(server_fd + 1, &readfds, NULL, NULL, &tv) > 0) {
            int bytes_received = recvfrom(server_fd, buffer, BUFFER_SIZE - 1, 0, // We have msg from client
                                         (struct sockaddr*)&client_addr, &client_len);
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                std::string msg(buffer);

                if(msg.find("RETRY_CHUNK ") ==0){
                    int chunk_num = std::stoi(msg.substr(12)); // Extract chunk number to retry
                    std::cout << "[Received] Retry request for chunk " << chunk_num + 1 << "/" << total_chunks << std::endl;

                    int current_chunk_size = (chunk_num == total_chunks - 1) ? // Calc chunk size
                                          (file_size - chunk_num * CHUNK_SIZE) : CHUNK_SIZE;

                    std::string header = "CHUNK " + std::to_string(chunk_num) + "/" +
                                      std::to_string(total_chunks - 1) + " "; // Preparing header

                    std::vector<char> chunk_buffer(header.length() + current_chunk_size);
                    memcpy(chunk_buffer.data(), header.c_str(), header.length());
                    memcpy(chunk_buffer.data() + header.length(),
                           image_data.data() + chunk_num * CHUNK_SIZE, current_chunk_size); // Buffer for chunk w/header

                    sendto(server_fd, chunk_buffer.data(), chunk_buffer.size(), 0,
                           (struct sockaddr*)&client_addr, client_len); // Send chunk

                    std::cout << "[Resent] Image chunk " << chunk_num + 1 << "/" << total_chunks << std::endl;

                    chunk_acked[chunk_num] = false; // Mark as unACKed to continue loop
                    all_chunks_acked = false;
                }
                else if (msg.find("ACK_CHUNK ") == 0) {
                    int chunk_num = std::stoi(msg.substr(10)); // Extract ACked chunk number
                    chunk_acked[chunk_num] = true;
                    std::cout << "[Received] Acknowledgment for chunk " << chunk_num + 1 << "/" << total_chunks << std::endl;
                }
            }
        }

        for (int i = 0; i < total_chunks; i++) { // Send unACKed chunks
            if (!chunk_acked[i]) {
                all_chunks_acked = false; // Found unacked chunk-> we aren't done then

                int current_chunk_size = (i == total_chunks - 1) ? // Finding size
                                          (file_size - i * CHUNK_SIZE) : CHUNK_SIZE;

                std::string header = "CHUNK " + std::to_string(i) + "/" + // Header
                                      std::to_string(total_chunks - 1) + " ";

                std::vector<char> chunk_buffer(header.length() + current_chunk_size);
                memcpy(chunk_buffer.data(), header.c_str(), header.length());
                memcpy(chunk_buffer.data() + header.length(),
                       image_data.data() + i * CHUNK_SIZE, current_chunk_size); // Buffer for chunk w/header

                sendto(server_fd, chunk_buffer.data(), chunk_buffer.size(), 0,
                       (struct sockaddr*)&client_addr, client_len); // Send forward chunk
                std::cout << "[Sent] Image chunk " << i + 1 << "/" << total_chunks << std::endl;



                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Wait a bit and send next chunk
                //just for extra safety
                // Only send one chunk at a time, then check for acks
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Short sleep just for extra safety
    }

    bool complete_acked = false; // Send msg
    int complete_retries = 0;
    std::string complete_msg = "IMAGE_COMPLETE";

    while(!complete_acked && complete_retries < IMAGE_MAX_RETRIES) {
        sendto(server_fd, complete_msg.c_str(), complete_msg.length(), 0,
               (struct sockaddr*)&client_addr, client_len);
        std::cout << "[Sent] Image transfer complete notification" << std::endl;

        fd_set readfds; // Set up timeout for ack
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        tv.tv_sec = 2;//2s timeout
        tv.tv_usec = 0;

        if(select(server_fd + 1, &readfds, NULL, NULL, &tv) > 0) {
            int bytes_received =recvfrom(server_fd, buffer, BUFFER_SIZE - 1, 0,
                                         (struct sockaddr*)&client_addr, &client_len);
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                std::string ack(buffer);
                if (ack == "ACK_COMPLETE") {
                    complete_acked = true;
                    std::cout << "[Received] Completion acknowledgment from client" << std::endl;
                }
            }
        } else {
            complete_retries++;
            std::cout << "[TIMEOUT] No completion acknowledgment, retrying... (Attempt " << complete_retries << ")\n";
        }
    }

    std::cout << "[INFO] Image transfer complete" << std::endl;
}

void handleRequest(int port) { // Handles requests on a specific port, runs on its own thread
    SOCKET server_fd; // Socket instance
    struct sockaddr_in server_addr{}, client_addr{}; //Structs for storing addresses
    char buffer[BUFFER_SIZE];// Buffer for incoming data
    int client_len = sizeof(client_addr); // Storing size of client address struct

    server_fd = socket(AF_INET, SOCK_DGRAM, 0);// Make UDP socket
    if (server_fd == INVALID_SOCKET){// Just for resiliency if we get failure
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
                else if (port == PORT_IMAGE) { // Handle image req separately
                    handleImageRequest(server_fd, client_addr, client_len, expectedSeqNum);
                    expectedSeqNum = 1 - expectedSeqNum; // Flip expected seq num, can do this differently too
                    continue; // Skip the rest of this...
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
    std::thread imageThread(handleRequest, PORT_IMAGE); // Create a thread for image requests

    rockThread.join(); // Wait for the rock thread to finish
    tempThread.join(); // Same thing for temperature
    monolithThread.join(); // And monoliths...
    radiationThread.join(); // And radiation
    imageThread.join(); // And images

    WSACleanup(); //Clean-up Winsock before exiting
    return 0; // Exit... and we are done
}