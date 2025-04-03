//SERVER.CPP
// WE RECOMMEND READING THE COMMENTS IN CLIENT.CPP AS A GOOD INTRO TO HOW OUR CODE WORKS
//AND SOME FUNCTIONS WE USE A LOT. THANKS

#include <iostream>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ctime>
#include <cstdlib>
#include <thread>
#include <fstream>
#include <vector>
#include <map>
#include <mutex>
#pragma comment(lib, "ws2_32.lib")
#define PORT_SPEED 12343
#define PORT_DEGREE 12344
#define PORT_ROCK 12345
#define PORT_TEMP 12346
#define PORT_MONOLITH 12347
#define PORT_RADIATION 12348
#define PORT_IMAGE 12349
#define PORT_ROVER_BROADCAST 12350 // Broadcasting (internal)
#define PORT_ROVER_DIRECT 5555// Direct rover-to-rover communication (TCP msgs w/alanis)
#define PORT_ROVER_COMMAND 5556 // Handling rover commands from Earth
#define PORT_FRIEND_DISCOVERY 5600 // Friend's discovery port (i.e. discovering Alanis)
#define BUFFER_SIZE 256
#define CHUNK_SIZE 512
#define IMAGE_MAX_RETRIES 5

//BELOW PART IS FOR HANDLING IMAGES
std::map<int, int> portSeqNums = {
    {PORT_ROCK, 0},
    {PORT_TEMP, 0},
    {PORT_MONOLITH, 0},
    {PORT_RADIATION, 0},
    {PORT_IMAGE, 0},
    {PORT_DEGREE, 0},
    {PORT_SPEED, 0}
};

std::map<std::string, sockaddr_in> connected_rovers; // Track connected rovers
std::mutex rovers_mutex; // Mutex basically means only one thread can access a resource at a time
//(useful to prevent connected_rovers being messed with)
int ORIENTATION = 0;
int SPEED = 0;
void changeDir(int degrees) { // This + next are Cristian's functions
    ORIENTATION = ORIENTATION + degrees;
    std::cout << "Direction changed by " << degrees << " degrees" << std::endl;
    std::cout << "Current direction = " << ORIENTATION << " degrees" << std::endl;
}

void changeSpeed(int speed) {
    SPEED = SPEED + speed;
    std::cout << "Speed changed by " << speed << " m/s" << std::endl;
    std::cout << "Current speed = " << SPEED << " m/s" << std::endl;
}

// New function for friend compatibility - sends discovery broadcast
void sendDiscoveryBroadcast() {
    SOCKET broadcast_sock = socket(AF_INET, SOCK_DGRAM, 0);
    BOOL broadcast_enabled = TRUE;
    if (setsockopt(broadcast_sock, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast_enabled, sizeof(broadcast_enabled)) == SOCKET_ERROR) { // Enables broadcast msgs
        std::cerr << "[ERROR] Couldnt set broadcast option: " << WSAGetLastError() << std::endl;
        closesocket(broadcast_sock);
        return;
    }

    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(PORT_FRIEND_DISCOVERY);
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");
    std::string broadcast_msg = "Lunar Rover Discovery"; // Alanis' rover needs to be able to recognise this so I'll check w/her once we meet
    if(sendto(broadcast_sock, broadcast_msg.c_str(), broadcast_msg.length(), 0, (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr)) == SOCKET_ERROR) {
        std::cerr <<"[ERROR] Failed to send discovery broadcast: " << WSAGetLastError() << std::endl;
    } else {
        std::cout << "[ROVER] Sent discovery broadcast" << std::endl;
    }

    closesocket(broadcast_sock);
}


void discoveryBroadcaster() { // Just broadcasts a discovery
    while (true) {
        sendDiscoveryBroadcast();
        std::this_thread::sleep_for(std::chrono::seconds(10)); // every 10s
    }
}

void friendDiscoveryListener() { // Listening for a friend...
    SOCKET listen_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(PORT_FRIEND_DISCOVERY);
    listen_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_sock, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Failed to bind discovery listener socket: " << WSAGetLastError() << std::endl;
        closesocket(listen_sock);
        return;
    }

    std::cout << "[ROVER] Listening for friend rover discovery on port " << PORT_FRIEND_DISCOVERY << std::endl;

    char buffer[BUFFER_SIZE];
    struct sockaddr_in sender_addr;
    int sender_len = sizeof(sender_addr);

    while (true) {
        int received = recvfrom(listen_sock, buffer, BUFFER_SIZE - 1, 0,(struct sockaddr*)&sender_addr, &sender_len);
        if (received > 0) {
            buffer[received] = '\0';

            char sender_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(sender_addr.sin_addr), sender_ip, INET_ADDRSTRLEN);

            std::string rover_ip(sender_ip);
            { // Store roveri n map
                std::lock_guard<std::mutex> lock(rovers_mutex);
                connected_rovers[rover_ip] = sender_addr;
            }

            std::cout << "[ROVER] Discovered friend rover at: " << sender_ip << std::endl;

            SOCKET ack_sock = socket(AF_INET, SOCK_STREAM, 0);// TCP ack
            if (ack_sock != INVALID_SOCKET) {
                struct sockaddr_in local_addr;
                memset(&local_addr, 0, sizeof(local_addr));
                local_addr.sin_family = AF_INET;
                local_addr.sin_port = htons(0); // assign port
                local_addr.sin_addr.s_addr = INADDR_ANY;

                if (bind(ack_sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) != SOCKET_ERROR) {
                    struct sockaddr_in target_addr;
                    memset(&target_addr, 0, sizeof(target_addr));
                    target_addr.sin_family = AF_INET;
                    target_addr.sin_port = htons(PORT_FRIEND_DISCOVERY);
                    inet_pton(AF_INET, sender_ip, &target_addr.sin_addr);

                    if (connect(ack_sock, (struct sockaddr*)&target_addr, sizeof(target_addr)) != SOCKET_ERROR) {
                        std::string response = "testing"; // Using Alanis' password
                        send(ack_sock, response.c_str(), response.length(), 0);
                    }
                }
                closesocket(ack_sock);
            }
        }
    }
}

void broadcastToRovers(const std::string& message) { // Broadcasts to all nearby rovers + attempt connection
    SOCKET broadcast_sock = socket(AF_INET, SOCK_DGRAM, 0); // UDP
    BOOL broadcast_enabled = TRUE; // ENable broadcasting on socket
    if (setsockopt(broadcast_sock, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast_enabled, sizeof(broadcast_enabled)) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Failed to set broadcast option: " << WSAGetLastError() << std::endl;
        closesocket(broadcast_sock);
        return;
    }

    struct sockaddr_in broadcast_addr;// Broadcast address
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(PORT_ROVER_BROADCAST);
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255"); // Broadcast to all interfaces
    std::string broadcast_msg = "BROADCAST_MSG: " + message;
    if (sendto(broadcast_sock, broadcast_msg.c_str(), broadcast_msg.length(), 0, //Send broadcast
              (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr)) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Failed to send broadcast: " << WSAGetLastError() << std::endl;
    } else {
        std::cout << "[ROVER] Broadcast message sent: " << message << std::endl;
    }
    closesocket(broadcast_sock);
    std::lock_guard<std::mutex> lock(rovers_mutex); // send to rfriendly rovers
    for (const auto& rover_pair : connected_rovers) {
        SOCKET friend_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (friend_sock == INVALID_SOCKET) continue;

        struct sockaddr_in friend_addr;
        memset(&friend_addr, 0, sizeof(friend_addr));
        friend_addr.sin_family = AF_INET;
        friend_addr.sin_port = htons(PORT_FRIEND_DISCOVERY); // Try Alanis' port
        friend_addr.sin_addr = rover_pair.second.sin_addr;

        if (connect(friend_sock, (struct sockaddr*)&friend_addr, sizeof(friend_addr)) != SOCKET_ERROR) {
            send(friend_sock, message.c_str(), message.length(), 0);
            std::cout << "[ROVER] Sent message to friend rover at " << rover_pair.first << std::endl;
        }

        closesocket(friend_sock);
    }
}

void sendDirectMessage(const std::string& ip, const std::string& message) {
    SOCKET direct_sock = socket(AF_INET, SOCK_STREAM, 0); // Standard port first
    if (direct_sock == INVALID_SOCKET) {
        std::cerr << "[ERROR] Failed to create direct message socket: " << WSAGetLastError() << std::endl;
        return;
    }
    struct sockaddr_in target_addr; //Target address
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(PORT_ROVER_DIRECT);
    if (inet_pton(AF_INET, ip.c_str(), &target_addr.sin_addr) <= 0) {
        std::cerr << "[ERROR] Invalid IP address: " << ip << std::endl;
        closesocket(direct_sock);
        return;
    }

    bool sent = false;

    if (connect(direct_sock, (struct sockaddr*)&target_addr, sizeof(target_addr)) != SOCKET_ERROR) {
        std::string direct_msg = "DIRECT_MSG: " + message;
        if (send(direct_sock, direct_msg.c_str(), direct_msg.length(), 0) != SOCKET_ERROR) {
            std::cout << "[ROVER] Direct message sent to " << ip << ": " << message << std::endl;
            sent = true;

            char ack_buffer[BUFFER_SIZE]; // ACCK wait
            int received = recv(direct_sock, ack_buffer, BUFFER_SIZE - 1, 0);
            if (received > 0) {
                ack_buffer[received] = '\0';
                std::cout << "[ROVER] Received acknowledgment: " << ack_buffer << std::endl;
            }
        }
    }

    closesocket(direct_sock);

    if (!sent) { //  try friends port
        SOCKET friend_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (friend_sock != INVALID_SOCKET) {
            struct sockaddr_in friend_addr;
            memset(&friend_addr, 0, sizeof(friend_addr));
            friend_addr.sin_family = AF_INET;
            friend_addr.sin_port = htons(PORT_FRIEND_DISCOVERY);
            inet_pton(AF_INET, ip.c_str(), &friend_addr.sin_addr);

            if (connect(friend_sock, (struct sockaddr*)&friend_addr, sizeof(friend_addr)) != SOCKET_ERROR) {
                std::string auth = "testing"; // First send authentication
                if (send(friend_sock, auth.c_str(), auth.length(), 0) != SOCKET_ERROR) {
                    // Wait a littl ebit
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));

                    if (send(friend_sock, message.c_str(), message.length(), 0) != SOCKET_ERROR) {
                        std::cout << "[ROVER] Message sent to friend rover at " << ip << std::endl; // Send msg
                    }
                }
            }

            closesocket(friend_sock);
        }
    }
}

void broadcastListener() {
    SOCKET broadcast_listen_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (broadcast_listen_sock == INVALID_SOCKET) {
        std::cerr << "[ERROR] Failed to create broadcast listener socket: " << WSAGetLastError() << std::endl;
        return;
    }
    struct sockaddr_in listen_addr; //Listen address
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(PORT_ROVER_BROADCAST);
    listen_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    if (bind(broadcast_listen_sock, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Failed to bind broadcast listener socket: " << WSAGetLastError() << std::endl;
        closesocket(broadcast_listen_sock);
        return;
    }

    std::cout << "[ROVER] Listening for broadcasts on port " << PORT_ROVER_BROADCAST << std::endl;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in sender_addr;
    int sender_len = sizeof(sender_addr);

    while (true) {
        int received = recvfrom(broadcast_listen_sock, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr*)&sender_addr, &sender_len);
        if (received > 0) {
            buffer[received] = '\0';
            char sender_ip[INET_ADDRSTRLEN]; //Sender IP
            inet_ntop(AF_INET, &(sender_addr.sin_addr), sender_ip, INET_ADDRSTRLEN);
            std::string rover_ip(sender_ip); // Rover->rovers map
            { // Lock mechanism from mutex (described above)
                std::lock_guard<std::mutex> lock(rovers_mutex);
                connected_rovers[rover_ip] = sender_addr;
            }

            std::cout << "[ROVER] Received broadcast from rover at " << sender_ip << ": " << buffer << std::endl;
        }
    }
}

void directMessageListener() {
    SOCKET direct_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (direct_listen_sock == INVALID_SOCKET) {
        std::cerr << "[ERROR] Failed to create direct message listener socket: " << WSAGetLastError() << std::endl;
        return;
    }
    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(PORT_ROVER_DIRECT);
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(direct_listen_sock, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Failed to bind direct message listener socket: " << WSAGetLastError() << std::endl;
        closesocket(direct_listen_sock);
        return;
    }
    if (listen(direct_listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Failed to listen for direct messages: " << WSAGetLastError() << std::endl;
        closesocket(direct_listen_sock);
        return;
    }
    std::cout << "[ROVER] Listening for direct messages on port " << PORT_ROVER_DIRECT << std::endl;
    while (true) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);

        SOCKET client_sock = accept(direct_listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock == INVALID_SOCKET) {
            std::cerr << "[ERROR] Failed to accept connection: " << WSAGetLastError() << std::endl;
            continue;
        }
        char client_ip[INET_ADDRSTRLEN]; // Client IP for displaying later on
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        std::string rover_ip(client_ip); // storing again in map
        {
            std::lock_guard<std::mutex> lock(rovers_mutex);
            connected_rovers[rover_ip] = client_addr;
        }

        char buffer[BUFFER_SIZE]; //Receive msg
        int received = recv(client_sock, buffer, BUFFER_SIZE -1, 0);
        if (received > 0) {
            buffer[received] = '\0';
            std::cout << "[ROVER] Received direct message from rover at " << client_ip << ": " << buffer << std::endl;

            std::string ack = "Message received"; // Acknowledgement
            send(client_sock, ack.c_str(), ack.length(), 0);
        }

        closesocket(client_sock);
    }

}

void roverCommandHandler() {
    SOCKET command_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (command_sock == INVALID_SOCKET) {
        std::cerr << "[ERROR] Failed to create rover command socket: " << WSAGetLastError() << std::endl;
        return;
    }
    struct sockaddr_in command_addr; // Command address
    memset(&command_addr, 0, sizeof(command_addr));
    command_addr.sin_family = AF_INET;
    command_addr.sin_port = htons(PORT_ROVER_COMMAND);
    command_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(command_sock, (struct sockaddr*)&command_addr, sizeof(command_addr)) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Failed to bind rover command socket: " << WSAGetLastError() << std::endl;
        closesocket(command_sock);
        return;
    }
    std::cout << "[ROVER] Listening for rover commands on port " << PORT_ROVER_COMMAND << std::endl;

    char buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr;
    int client_len = sizeof(client_addr);
    while (true) {
        int received = recvfrom(command_sock, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr*)&client_addr, &client_len);
        if (received > 0) {
            buffer[received] = '\0';
            std::string command(buffer);
            std::cout << "[ROVER] Received command: " << command << std::endl;

            if (command.substr(0, 10) == "BROADCAST:") { // Just parsing stuff
                std::string message = command.substr(10);
                broadcastToRovers(message);
                std::string ack = "Broadcast sent successfully";
                sendto(command_sock, ack.c_str(), ack.length(), 0,
                      (struct sockaddr*)&client_addr, client_len);
            }
            else if (command.substr(0, 5) == "SEND:") {
                // Format is SEND:<ip>:<message>
                size_t first_colon = command.find(':');
                size_t second_colon = command.find(':', first_colon + 1);

                if (second_colon != std::string::npos) {
                    std::string ip = command.substr(first_colon + 1, second_colon - first_colon - 1);
                    std::string message = command.substr(second_colon + 1);

                    sendDirectMessage(ip, message);

                    // Send acknowledgment
                    std::string ack = "Direct message sent successfully";
                    sendto(command_sock, ack.c_str(), ack.length(), 0,
                          (struct sockaddr*)&client_addr, client_len);
                }
                else {
                    std::string error = "Invalid command format";
                    sendto(command_sock, error.c_str(), error.length(), 0,
                          (struct sockaddr*)&client_addr, client_len);
                }
            }
            else {
                std::string error = "Unknown command";
                sendto(command_sock, error.c_str(), error.length(), 0,(struct sockaddr*)&client_addr, client_len);
            }
        }
    }
}

void handleImageRequest(SOCKET server_fd, struct sockaddr_in client_addr, int client_len, int seq_num) {
    std::string image_path; //Image depends on orientation
    int normalized_orientation = ((ORIENTATION % 360) + 360) % 360; // Lock between 0 and 359 degrees
    if (normalized_orientation >= 0 && normalized_orientation < 90) { //and make image depend on orientation
        image_path = "moon_image1.jpg";
    } else if (normalized_orientation >= 90 && normalized_orientation < 180) {
        image_path = "moon_image2.jpg";
    } else if (normalized_orientation >= 180 && normalized_orientation < 270) {
        image_path = "moon_image3.jpg";
    } else {
        image_path = "moon_image4.jpg";
    }


    std::cout << "[INFO] Sending image for orientation " << normalized_orientation << " degrees: " << image_path << std::endl;
    std::ifstream file(image_path, std::ios::binary);
    if (!file){
        std::string error_msg = "ACK " + std::to_string(seq_num) + " | IMAGE: ERROR_LOADING"; // Error msg format
        sendto(server_fd, error_msg.c_str(), error_msg.length(), 0, (struct sockaddr*)&client_addr, client_len);
        std::cerr << "[ERROR] Couldn't open image file: " << image_path << std::endl;
        return;
    }
    file.seekg(0, std::ios::end); // Get file size by moving pointer to end
    size_t file_size = file.tellg(); // Tells us current position of pointe
    file.seekg(0, std::ios::beg); // Go back to beginning
    std::vector<char> image_data(file_size); // + read into buffer by creating vector of adequate size
    file.read(image_data.data(), file_size); // Read to memory aaand
    file.close();// close
    std::string size_msg = "ACK " + std::to_string(seq_num) + " | IMAGE_SIZE: " + std::to_string(file_size);
    bool size_acked = false;
    int size_retries = 0;
    char buffer[BUFFER_SIZE];
    while(!size_acked && size_retries < IMAGE_MAX_RETRIES) { // While not ACKed
        sendto(server_fd, size_msg.c_str(), size_msg.length(), 0, (struct sockaddr*)&client_addr, client_len);
        std::cout << "[Sent] " << size_msg << std::endl; // Send to client + print
        //We need something non-blocking (recvfrom is blocking) so that we can get a timeout working
        fd_set readfds; //readfds -> read file descriptor, stores all sockets we wanna monitor
        struct timeval tv; // For timeout duration stuff, contains tv_sec (no. seconds to wait) and tv_usec (no. microsecs to wait)
        FD_ZERO(&readfds); // Sets readfds to empty set
        FD_SET(server_fd, &readfds); // Adds socket to readfds, so we later essentially tell select to monitor the socket to see if there's anything to read
        tv.tv_sec = 4; //4 seconds
        tv.tv_usec = 0; //0 microsecs

        if(select(server_fd + 1,&readfds, NULL, NULL, &tv) > 0) {
            //Select() parameters: highest number file descriptor, set of sockets, two sets we needn't worry about, and timeout
            int bytes_received = recvfrom(server_fd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr*)&client_addr, &client_len);
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                std::string ack(buffer);
                if (ack == "ACK_SIZE") {
                    size_acked = true;
                    std::cout << "[Received] Size acknowledgment from client" << std::endl;
                }
            }
        }else {
            size_retries++; // need to retry
            std::cout << "[TIMEOUT] No size acknowledgment, retrying... (Attempt " << size_retries << ")\n";
        }
    }
    if (!size_acked) { // Below here is mostly data integrity stuff
        std::cerr << "[ERROR] Failed to send image size after " << IMAGE_MAX_RETRIES << " attempts" << std::endl;
        return;
    }
    int total_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE; // Finds no. chunks needed for image, ceiling div
    std::map<int, bool> chunk_acked; // Can use a map of chunks and whether theyre acked or not for tracking
    for (int i = 0; i < total_chunks;i++){ // Tracking ACked chunks
        chunk_acked[i] = false;
    }
    bool all_chunks_acked = false;
    while (!all_chunks_acked) { // Keep trying until all acked
        all_chunks_acked = true;
        fd_set readfds; // Check for retry requests/send next unacked chunk
        struct timeval tv;
        FD_ZERO(&readfds); // Clears
        FD_SET(server_fd, &readfds); //and adds our socket...
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        if (select(server_fd + 1, &readfds, NULL, NULL, &tv) > 0) {
            int bytes_received = recvfrom(server_fd, buffer, BUFFER_SIZE - 1, 0,(struct sockaddr*)&client_addr, &client_len);
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                std::string msg(buffer);
                if(msg.find("RETRY_CHUNK ") ==0){
                    int chunk_num = std::stoi(msg.substr(12)); // Chunk no. to retry (12 go to pos 12 bcs RETRY CHUNK is 12 chars long)
                    std::cout << "[Received] Retry request for chunk " << chunk_num + 1 << "/"<< total_chunks << std::endl;

                    int current_chunk_size = (chunk_num == total_chunks - 1) ? (file_size - chunk_num * CHUNK_SIZE) : CHUNK_SIZE; // Checks if we're sending final chunk, and if so it's fiel_size-chunk_num*chunk size
                    std::string header = "CHUNK " + std::to_string(chunk_num) + "/" + std::to_string(total_chunks - 1) + " "; // Header

                    std::vector<char> chunk_buffer(header.length() + current_chunk_size); // Byte buffer for header + chunk size
                    memcpy(chunk_buffer.data(), header.c_str(), header.length()); // Copies headder into buffer
                    memcpy(chunk_buffer.data() + header.length(), image_data.data() + chunk_num * CHUNK_SIZE, current_chunk_size); // Copies image data to buffer


                    sendto(server_fd, chunk_buffer.data(), chunk_buffer.size(), 0,(struct sockaddr*)&client_addr, client_len); // Send chunk
                    std::cout << "[Resent] Image chunk " << chunk_num + 1 << "/" << total_chunks << std::endl;
                    chunk_acked[chunk_num] = false;
                    all_chunks_acked = false;
                }
                else if (msg.find("ACK_CHUNK ") == 0) {
                    int chunk_num = std::stoi(msg.substr(10)); // Find ACked chunk number (ACK_CHUNK  is 10 bytes)
                    chunk_acked[chunk_num] = true;
                    std::cout << "[Received] Acknowledgment for chunk " << chunk_num +1 << "/" << total_chunks << std::endl;
                }
            }
        }
        for (int i = 0; i < total_chunks; i++) {
            if (!chunk_acked[i]) {
                all_chunks_acked = false; // Need to finish ACking
                int current_chunk_size = (i == total_chunks - 1) ? (file_size - i * CHUNK_SIZE) : CHUNK_SIZE; // Logic repeated a few lines above
                std::string header = "CHUNK " + std::to_string(i) + "/" + std::to_string(total_chunks - 1) + " ";
                std::vector<char> chunk_buffer(header.length() + current_chunk_size);
                memcpy(chunk_buffer.data(), header.c_str(), header.length()); // Copies to start of buffer
                memcpy(chunk_buffer.data() + header.length(), image_data.data() + i * CHUNK_SIZE, current_chunk_size); // Actual binary image chunk

                sendto(server_fd, chunk_buffer.data(), chunk_buffer.size(), 0, (struct sockaddr*)&client_addr, client_len); // Send forward chunk
                std::cout << "[Sent] Image chunk " << i + 1 << "/" << total_chunks << std::endl;
                // JUST SOMETHING TO NOTE HERE: CLIENT IN THE CONTEXT OF SERVER.CPP IS RELAY.CPP BECAUSE WE SEND DATA THERE
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                break;
            }
        }
    }

    bool complete_acked = false;
    int complete_retries = 0;
    std::string complete_msg = "IMAGE_COMPLETE";
    while(!complete_acked && complete_retries < IMAGE_MAX_RETRIES) { // If we should retry...
        sendto(server_fd, complete_msg.c_str(), complete_msg.length(), 0,(struct sockaddr*)&client_addr, client_len);
        std::cout << "[Sent] Image transfer complete notification" << std::endl;
        fd_set readfds; // Set up timeout for ack
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        tv.tv_sec = 4;
        tv.tv_usec = 0;

        if(select(server_fd + 1, &readfds, NULL, NULL, &tv) > 0) {// Waits for socket to become ready to read,
            //monitors for readability, and can listen non-blocking style
            int bytes_received =recvfrom(server_fd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr*)&client_addr, &client_len);
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
            std::cout << "[TIMEOUT] No completion acknowledgment,retrying. (Attempt " << complete_retries << ")\n";
        }
    }
    std::cout << "[INFO] Image transfer complete" << std::endl;// WE ARE DONE
}

void handleRequest(int port) { //HANDLES REQUEST
    SOCKET server_fd; // Similar to everything in client.cpp
    struct sockaddr_in server_addr{}, client_addr{};
    char buffer[BUFFER_SIZE];
    int client_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd == INVALID_SOCKET){
        std::cerr << "[ERROR] Socket creation failed on port " << port << ": " <<WSAGetLastError() << std::endl;
        return;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if(bind(server_fd,(struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Bind failed on port " << port << ": " << WSAGetLastError() <<std::endl;
        closesocket(server_fd); //Cleanup...
        return;
    }

    std::cout << "[INFO] Listening on port " << port << "...\n";
    int expectedSeqNum = 0; // RDT 3.0 sequence tracker(per threadb asis)
    std::string lastResponse = "";// Used if last good packet must be sent i.e. if wrong ACK seq number is received, good for re-sending

    while (true) {
        int bytes_received = recvfrom(server_fd, buffer, BUFFER_SIZE -1, 0, (struct sockaddr*)&client_addr, &client_len);
        if (bytes_received > 0){
            buffer[bytes_received] = '\0';
            std::time_t now = std::time(nullptr); // Cur. time
            std::string received(buffer);
            std::cout<< "[Received @ " << std::ctime(&now) << "] " << received << std::endl; // Print time

            if (received.rfind("SEQ " + std::to_string(expectedSeqNum), 0) == 0) { // Only accept expected seq num
                std::string response;
                if (port == PORT_ROCK) { // Sorting ports to answer on...
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
                else if (port == PORT_DEGREE) {
                    std::string data = received; // Get degree value from the message
                    int degree;
                    size_t last_space = data.find_last_of(' ');
                    if (last_space != std::string::npos) {
                        std::string last_num_str = data.substr(last_space + 1);
                        degree = std::stoi(last_num_str);
                        std::cout << "Extracted number: " << degree << std::endl;
                    }
                    changeDir(degree);
                    response = "ACK " + std::to_string(expectedSeqNum) + " | DEGREES ALTERED BY: " + std::to_string(degree) + ", current orientation = " + std::to_string(ORIENTATION) + " degrees";
                }
                else if (port == PORT_SPEED) {
                    std::string data = received; // Get speed val
                    int speed;
                    size_t last_space = data.find_last_of(' ');
                    if (last_space != std::string::npos) {
                        std::string last_num_str = data.substr(last_space + 1);
                        speed = std::stoi(last_num_str);
                        std::cout << "Extracted number: " << speed << std::endl;
                    }
                    changeSpeed(speed);
                    response = "ACK " + std::to_string(expectedSeqNum) + " | SPEED ALTERED BY: " + std::to_string(speed) + ", current speed = " + std::to_string(SPEED) + " m/s";
                }
                else if (port == PORT_IMAGE) { // Handle image req separately
                    handleImageRequest(server_fd, client_addr, client_len, expectedSeqNum);
                    expectedSeqNum = 1 - expectedSeqNum; //flipping...
                    lastResponse = "";
                    continue; // done
                }

                lastResponse = response;
                sendto(server_fd, response.c_str(),response.length(), 0, (struct sockaddr*)&client_addr, client_len); // Send the response back to the client
                std::cout << "[Sent] " << response << std::endl;
                expectedSeqNum = 1 - expectedSeqNum;
            }else{
                std::cout << "[DUPLICATE/OUT-OF-ORDER] Resending last response: " << lastResponse << std::endl; // Just resend
                sendto(server_fd, lastResponse.c_str(), lastResponse.length(), 0, (struct sockaddr*)&client_addr, client_len);
            }
        }
    }
}

int main() {
    WSADATA wsaData;
    int wsaerr = WSAStartup(MAKEWORD(2, 2), &wsaData); // Initialise Winsock v2.2
    if (wsaerr != 0) {
        std::cerr << "[ERROR] WSAStartup failed: " << wsaerr << std::endl;
        return 1;
    }

    std::srand(std::time(nullptr)); // Seed the random number generator for realistic randomness, but in reality pseudorandom

    // Initialize rover-to-rover communication threads
    std::thread broadcastListenerThread(broadcastListener);
    std::thread directMessageListenerThread(directMessageListener);
    std::thread commandHandlerThread(roverCommandHandler);

    // Add new threads for friend rover compatibility
    std::thread discoveryBroadcastThread(discoveryBroadcaster);
    std::thread friendDiscoveryListenerThread(friendDiscoveryListener);

    // Detach rover communication threads to run in background
    broadcastListenerThread.detach();
    directMessageListenerThread.detach();
    commandHandlerThread.detach();
    discoveryBroadcastThread.detach();
    friendDiscoveryListenerThread.detach();

    std::cout << "[ROVER] Rover-to-rover communication initialized" << std::endl;
    std::cout << "[ROVER] Friend rover compatibility enabled" << std::endl;

    // Create original service threads
    std::thread rockThread(handleRequest, PORT_ROCK); // We make a thread for each type of request
    std::thread tempThread(handleRequest, PORT_TEMP);
    std::thread monolithThread(handleRequest, PORT_MONOLITH);
    std::thread radiationThread(handleRequest, PORT_RADIATION);
    std::thread imageThread(handleRequest, PORT_IMAGE);
    std::thread degreeThread(handleRequest, PORT_DEGREE); // Cristian's threads
    std::thread speedThread(handleRequest, PORT_SPEED);

    // Join threads
    speedThread.join();
    degreeThread.join();
    rockThread.join();
    tempThread.join();
    monolithThread.join();
    radiationThread.join();
    imageThread.join();

    WSACleanup();
    return 0; // Exit... and we are done
}