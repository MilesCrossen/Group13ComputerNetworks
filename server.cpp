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
#define PORT_DISCOVERY 12350 // New port for rover discovery
#define DISCOVERY_BUFFER_SIZE 512 // Buffer for discovery responses
#define ROVER_PORT 13000 // Port for direct rover-to-rover communication

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

void handleDiscoveryRequest(int seq_num) { // Sequence num as parameter to prevent desynchronisation
    SOCKET discovery_fd; // Socket instance for discovery communication
    struct sockaddr_in discovery_addr{}; // Struct for storing socket address info

    discovery_fd = socket(AF_INET, SOCK_DGRAM, 0); // Create UDP socket for discovery (connectionless)
    if (discovery_fd == INVALID_SOCKET) { // Check if socket creation failed (robustness)
        std::cerr << "[ERROR] Discovery socket creation failed: " << WSAGetLastError() << std::endl;
        return; // Exit if we can't even create a socket... no point continuing
    }

    // Enable broadcasting since discovery needs to receive broadcast messages
    int broadcast = 1; // 1 = enable broadcasting, 0 = disable
    if (setsockopt(discovery_fd, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof(broadcast)) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Could not set broadcast option: " << WSAGetLastError() << std::endl;
        closesocket(discovery_fd); // Clean up the socket before exiting
        return; // No point continuing if we can't receive broadcasts
    }

    // Set up the socket address structure for binding
    discovery_addr.sin_family = AF_INET; // Using IPv4...
    discovery_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all network interfaces
    discovery_addr.sin_port = htons(PORT_DISCOVERY); // Convert port to network byte order i.e. number->binary

    if (bind(discovery_fd, (struct sockaddr*)&discovery_addr, sizeof(discovery_addr)) == SOCKET_ERROR) { // Binding socket
        std::cerr << "[ERROR] Bind failed on discovery port: " << WSAGetLastError() << std::endl;
        closesocket(discovery_fd); // Cleanup...
        return; // Again, can't continue if binding fails
    }

    std::cout << "[INFO] Listening for discovery requests on port " << PORT_DISCOVERY << "...\n";
    // Buffer for receiving discovery requests and struct for client address
    char buffer[DISCOVERY_BUFFER_SIZE]; // Buffer for incoming discovery messages
    struct sockaddr_in client_addr{}; // Will store the address of whoever is discovering us
    int client_len = sizeof(client_addr); // Size needed for recvfrom()

    // Generate unique rover identifier using hostname and process ID
    char hostname[256]; // Buffer to store this computer's hostname
    gethostname(hostname, sizeof(hostname)); // Get the actual hostname
    DWORD pid = GetCurrentProcessId(); // Get process ID to make ID even more unique

    // Create rover information that will be sent in discovery responses
    std::string rover_id = std::string(hostname) + "-" + std::to_string(pid); // Unique ID combining hostname and PID
    std::string rover_capabilities = "rock_analysis,temperature,radiation,imagery"; // What this rover can do
    std::string rover_status = "active"; // Current operational status

    while(true) { // Main discovery loop ->keep listening for discovery requests forever
        int bytes_received = recvfrom(discovery_fd, buffer, DISCOVERY_BUFFER_SIZE - 1, 0,
                              (struct sockaddr*)&client_addr, &client_len); // Wait for an incoming discovery request

        if (bytes_received > 0) { // Only process if we actually received sth
            buffer[bytes_received] = '\0'; // Null-terminate for string processing
            std::string request(buffer); // Convert to string object for easier handling
            std::time_t now = std::time(nullptr); // Get current time for logging
            std::cout << "[Received Discovery @ " << std::ctime(&now) << "] " << request << std::endl;

            // Check if this is a properly formatted discovery request with the expected sequence number
            if (request.rfind("SEQ " + std::to_string(seq_num) + " | DISCOVER_ROVERS", 0) == 0) {
                // We need our own IP address to include in the response
                char local_ip[INET_ADDRSTRLEN]; // Buffer for IP string representation
                struct sockaddr_in local_addr{}; // Struct to hold our local address info
                int local_addr_len = sizeof(local_addr);

                // Get the socket's local address info
                getsockname(discovery_fd, (struct sockaddr*)&local_addr, &local_addr_len);
                // Convert binary IP address to string format
                inet_ntop(AF_INET, &local_addr.sin_addr, local_ip, INET_ADDRSTRLEN);

                // Format our discovery response with all rover details
                // Format: ACK [seq] | ROVER_INFO: [id],[ip],[capabilities],[status]
                std::string response = "ACK " + std::to_string(seq_num) + " | ROVER_INFO: " +
                                      rover_id + "," +
                                      local_ip + "," +
                                      rover_capabilities + "," +
                                      rover_status;

                // Send our response back to whoever sent the discovery request
                sendto(discovery_fd, response.c_str(), response.length(), 0,
                      (struct sockaddr*)&client_addr, client_len);

                std::cout << "[Sent] " << response << std::endl; // Log what we sent
            }
        }
    }

    closesocket(discovery_fd); // Close socket before exiting but not necessary as outlined in diff functions
}

void handleRoverRequests() {
    SOCKET rover_fd; // Socket instance for rover-to-rover communication
    struct sockaddr_in rover_addr{}, client_addr{}; // Address structs for this rover and others
    char buffer[BUFFER_SIZE]; // Buffer for incoming data
    int client_len = sizeof(client_addr); // Size of client address struct needed for recvfrom

    // Create a UDP socket for rover-to-rover communication
    rover_fd = socket(AF_INET, SOCK_DGRAM, 0); // UDP socket is connectionless and good for our purpose
    if (rover_fd == INVALID_SOCKET) { // Check if socket creation failed
        std::cerr << "[ERROR] Rover socket creation failed: " << WSAGetLastError() << std::endl;
        return; // Exit if we can't create a socket
    }

    rover_addr.sin_family = AF_INET; // IPv4 address family
    rover_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all available network interfaces
    rover_addr.sin_port = htons(ROVER_PORT); // Convert rover port to network byte order

    if (bind(rover_fd, (struct sockaddr*)&rover_addr, sizeof(rover_addr)) == SOCKET_ERROR) { // Bind socket to port
        std::cerr << "[ERROR] Bind failed on rover port: " << WSAGetLastError() << std::endl;
        closesocket(rover_fd); // Clean up socket resource
        return; // Can't continue if binding fails
    }

    std::cout << "[INFO] Listening for direct rover messages on port " << ROVER_PORT << "...\n";

    // Track sequence numbers for rover-to-rover communication
    int expectedRoverSeqNum = 0; // Start with sequence number 0 for RDT 3.0
    std::string lastRoverResponse = ""; // Store last response in case we need to resend

    while (true) { // Infinite loop to continuously handle messages
        int bytes_received = recvfrom(rover_fd, buffer, BUFFER_SIZE - 1, 0,
                              (struct sockaddr*)&client_addr, &client_len); // Wait for data from other rovers

        if (bytes_received > 0) { // Only process if we actually received something
            buffer[bytes_received] = '\0'; // Null-terminate the string
            std::string request(buffer); // Convert to string for easier processing
            std::time_t now = std::time(nullptr); // Current time for logging
            std::cout << "[Rover Message @ " << std::ctime(&now) << "] " << request << std::endl;

            // Check if this is a new request with the expected sequence number
            if (request.rfind("ROVER_SEQ " + std::to_string(expectedRoverSeqNum), 0) == 0) {
                std::string response; // Will hold our response to the request

                if (request.find("REQ TEXT") != std::string::npos) { // Text message handling
                    // Extract text content starting after "REQ TEXT "
                    std::string text_content = request.substr(request.find("REQ TEXT") + 9);
                    std::cout << "[TEXT MESSAGE RECEIVED] " << text_content << std::endl; // Display received text
                    response = "ROVER_ACK " + std::to_string(expectedRoverSeqNum) +
                              " | TEXT_RECEIVED: " + text_content; // Echo back the text
                }
                else { // Unknown request type
                    response = "ROVER_ACK " + std::to_string(expectedRoverSeqNum) +
                              " | ERROR: Unknown request type";
                }

                lastRoverResponse = response; // Save response in case we need to resend
                sendto(rover_fd, response.c_str(), response.length(), 0,
                      (struct sockaddr*)&client_addr, client_len); // Send response to requesting rover
                std::cout << "[Rover Response] " << response << std::endl;
                expectedRoverSeqNum = 1 - expectedRoverSeqNum; // Flip sequence number for RDT 3.0
            }
            else { // Out of order or duplicate packet
                // Resend last response for duplicate/out-of-order packets
                std::cout << "[DUPLICATE/OUT-OF-ORDER] Resending last response: " << lastRoverResponse << std::endl;
                sendto(rover_fd, lastRoverResponse.c_str(), lastRoverResponse.length(), 0,
                      (struct sockaddr*)&client_addr, client_len); // Resend last response
            }
        }
    }

    closesocket(rover_fd); // Close socket before exiting (unlikely to reach here)
}

int main() {
    WSADATA wsaData; // This holds Winsock startup data
    int wsaerr = WSAStartup(MAKEWORD(2, 2), &wsaData); // Initialise Winsock v2.2
    if (wsaerr != 0) { // If failure, print error and exit
        std::cerr << "WSAStartup failed: " << wsaerr << std::endl;
        return 1; // 1 = error usually
    }
    std::srand(std::time(nullptr)); // Seed the random number generator for realistic randomness, but in reality pseudorandom

    std::thread rockThread(handleRequest, PORT_ROCK); //Create a thread to handle rock data requests
    std::thread tempThread(handleRequest, PORT_TEMP); // Create a thread to handle temperature requests
    std::thread monolithThread(handleRequest, PORT_MONOLITH); // Create a thread for monolith requests
    std::thread radiationThread(handleRequest, PORT_RADIATION); // Create a thread for radiation data
    std::thread imageThread(handleRequest, PORT_IMAGE); // Create a thread for image requests
    std::thread discoveryThread(handleDiscoveryRequest, 0); // Create a thread for discovery requests with initial seq_num = 0
    std::thread roverThread(handleRoverRequests); // Create a thread for rover-to-rover communication

    rockThread.join(); // Wait for the rock thread to finish
    tempThread.join(); // Same thing for temperature
    monolithThread.join(); // And monoliths...
    radiationThread.join(); // And radiation
    imageThread.join(); // And images
    discoveryThread.join(); // Wait for discovery thread to finish
    roverThread.join();

    WSACleanup(); //Clean-up Winsock before exiting
    return 0; // Exit... and we are done
}

