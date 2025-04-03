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
#pragma comment(lib, "ws2_32.lib")
#define PORT_SPEED 12343
#define PORT_DEGREE 12344
#define PORT_ROCK 12345
#define PORT_TEMP 12346
#define PORT_MONOLITH 12347
#define PORT_RADIATION 12348
#define PORT_IMAGE 12349
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

    std::srand(std::time(nullptr)); // Seed the random number generator for realistic randomness, but in reality pseudorandom
    std::thread rockThread(handleRequest, PORT_ROCK); // We make a thread for each type of request
    std::thread tempThread(handleRequest, PORT_TEMP);
    std::thread monolithThread(handleRequest, PORT_MONOLITH);
    std::thread radiationThread(handleRequest, PORT_RADIATION);
    std::thread imageThread(handleRequest, PORT_IMAGE);
    std::thread degreeThread(handleRequest, PORT_DEGREE); // Cristian's threads
    std::thread speedThread(handleRequest, PORT_SPEED);

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