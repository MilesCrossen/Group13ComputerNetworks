//CLIENT.CPP
//BELOW HERE IS MOSTLY INITIALISATION

#include <iostream> // IOstream
#include <cstring>
#include <winsock2.h>// Socket programming for windows
#include <ws2tcpip.h> // Additional TCP/IP stuff
#include <thread>// Multi-threading, necessary bcs we need to do multiple things at once
#include <fstream> // For writing image file, file stream
#include <vector> // For storing image data
#include <map> // For tracking chunks
#include <regex> // For input parsing
#include <sstream> // For string stream processing
#pragma comment(lib, "ws2_32.lib") // Link Winsock library for networking
#define RELAY_IP "127.0.0.1" // Server IP
#define PORT_ROCK 5000 // Port spammm
#define PORT_TEMP 5001 // Prof McGoldrick requests diff ports to be used for diff things in his
#define PORT_MONOLITH 5002 //pdf so that's what we do
#define PORT_RADIATION 5003
#define PORT_IMAGE 5004 // New port for image requests
#define PORT_DEGREE 5005 // Port for direction controls
#define PORT_SPEED 5006 // Port for speed controls
#define PORT_ROVER_COMMAND 5557 // Rover-rover commands
#define BUFFER_SIZE 256 // This is all explained in other files tbh idk why I'd explain it again
#define MAX_RETRIES 5 // Retry count for lost packets
#define IMAGE_BUFFER_SIZE 2048// Larger buffer for image data
#define TIMEOUT 5000 // 5 second timeout for image operations

// So each transmission gets a diff sequence number because that's how rdt 3.0 (which is what we
std::map<int, int> portSeqNums = {//use works
    {PORT_ROCK, 0},
    {PORT_TEMP, 0},
    {PORT_MONOLITH, 0},
    {PORT_RADIATION, 0},
    {PORT_IMAGE, 0},
    {PORT_DEGREE, 0},
    {PORT_SPEED, 0}
};

void sendRoverCommand(const std::string& command) { //Rover-rover commands
    SOCKET command_socket = socket(AF_INET, SOCK_DGRAM, 0);
    int timeout = TIMEOUT;
    setsockopt(command_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    struct sockaddr_in relay_addr; // Relay address
    memset(&relay_addr, 0, sizeof(relay_addr)); // memset sets sizeof(relay_addr) bytes to 0 starting from &relayy_addr
    relay_addr.sin_family = AF_INET;
    relay_addr.sin_port = htons(PORT_ROVER_COMMAND);

    if(inet_pton(AF_INET, RELAY_IP, &relay_addr.sin_addr) <= 0) { // checks if result is valid
        std::cerr << "[ERROR] Invalid relay address\n";
        closesocket(command_socket);
        return;
    }

    if(sendto(command_socket, command.c_str(), command.length(), 0, (struct sockaddr*)&relay_addr, sizeof(relay_addr)) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Failed to send rover command: " << WSAGetLastError() << std::endl;
        closesocket(command_socket);
        return;
    } //
    std::cout << "[SENT] Rover command: " << command << std::endl;
    char buffer[BUFFER_SIZE]; // for waiting for ACK
    int relay_len = sizeof(relay_addr);
    int received = recvfrom(command_socket, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr*)&relay_addr, &relay_len);
    if (received > 0) { // Received?
        buffer[received] = '\0'; // Termination
        std::cout << "[RECEIVED] Rover command response: " << buffer << std::endl;
    } else {
        std::cerr << "[TIMEOUT] No response from rover command\n";
    }

    closesocket(command_socket);
}

void requestImage(){
    SOCKET sock_fd;
    struct sockaddr_in relay_addr; // So within sockaddr_in, we can store a bunch of useful socket data
    // like ports then the bind() function can bind the socket to this data
    char buffer[IMAGE_BUFFER_SIZE]; // Image data needs a buffer
    size_t image_size = 0; // image size (bytes)
    sock_fd = socket(AF_INET,SOCK_DGRAM, 0);
    // so socket() makes a socket, AF_INET means IPv4, SOCK_DGRAM is UDP, 0 selects appropriate protocol
    int timeout = TIMEOUT; // timeout in ms
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    // setsockopt changes how the socket behaves, sock_fd is our socket, SOL_SOCKET says what we're setting is socket
    //-level, RCVTIME0 is receive timeout duration, timeout is a timeout value of course. This is mostly just generall
    //formatting stuff
    relay_addr.sin_family = AF_INET; // IPv4
    relay_addr.sin_port = htons(PORT_IMAGE); // Image port, real value->binary
    if (inet_pton(AF_INET, RELAY_IP, &relay_addr.sin_addr) <= 0){
        std::cerr << "[ERROR] Invalid address for relay\n";
        closesocket(sock_fd);
        return; // This whole thing tells socket where to send data
    }



    //BELOW HERE WE ACTUALLY START DOING TRANSMISSION/DATA INTEGRITY STUFF, PARTICULARLY W/RDT3 (w/o pipelining)
    std::string packet = "SEQ " + std::to_string(portSeqNums[PORT_IMAGE]) +" | REQ IMAGE";
    int retries = 0;
    bool received_size = false; // Do we have image size?
    while (retries < MAX_RETRIES && !received_size) {
        sendto(sock_fd, packet.c_str(), packet.length(), 0, // for sending required packet to relay
               (struct sockaddr*)&relay_addr, sizeof(relay_addr));
        //sendto() and recfrom() are of the form (socket name, data buffer to send, length of data, 0 for udp, destination address, size of dest address)
        std::cout << "[Sent] " << packet << " on port " << PORT_IMAGE << std::endl;
        int relay_len = sizeof(relay_addr);// Wait for a response (the image size information)
        int bytes_received = recvfrom(sock_fd, buffer, IMAGE_BUFFER_SIZE - 1, 0,
                                     (struct sockaddr*)&relay_addr, &relay_len);

        if (bytes_received > 0){ // Checks for response, which is important bcs we aren't guaranteed a response through packet loss etc
            buffer[bytes_received] = '\0'; // Null-terminate
            std::string response(buffer); // String to store contents of buffer i..e what we receive

            if (response.rfind("ACK " +std::to_string(portSeqNums[PORT_IMAGE]), 0)== 0 && //expects seq number 0 at first.
                //Seq number flips after every correct tranmission so the expected val flips
                response.find("IMAGE_SIZE: ") != std::string::npos) { //.find searches string for a substring inside i.e. we search for "IMAGE SIZE: "
                size_t pos = response.find("IMAGE_SIZE: ") +12;// IMAGE_SIZE: is 12 chars long so we go 12 chars forward
                //to get the actual payload data
                std::string size_str = response.substr(pos); // Creates substring starting at pos (i.e. data)
                image_size = std::stoul(size_str); // Convert string to unsigned long
                std::cout << "[Server Response via Relay] Image size: " << image_size << " bytes" << std::endl;
                received_size = true; // Set flag that we got the size
                portSeqNums[PORT_IMAGE] = 1 - portSeqNums[PORT_IMAGE]; // Flip sequence number for this port

                sendto(sock_fd, "ACK_SIZE", 8, 0, // Send ack to relay
                      (struct sockaddr*)&relay_addr, sizeof(relay_addr));
            }else{
                std::cout << "[IGNORED] Unexpected response: " << response << std::endl;// Could be out of order packet or response meant for someon eelse
            }
        } else {
            // No response within timeout period? Go again
            std::cout << "[TIMEOUT] No response, retrying... (Attempt " << retries + 1 << ")\n"; retries++;
        }
    }

    std::ofstream output_file("received_image.jpg", std::ios::binary); // Creating file to store
    if (!output_file) {// If image not received even after retries?
        closesocket(sock_fd); // just close socket, not rly necessary but helps to avoid memory leaks
        return;
    }



    std::cout << "[INFO] Starting image transfer..." << std::endl;
    bool transfer_complete = false; // Tracking individual chunk transfers
    std::map<int, bool> chunk_received; // Tracks chunks by number
    std::map<int, std::vector<char>> chunk_data; // Stores actual data for each chunk
    int total_chunks = -1; // Increased once chunks received
    int transfer_timeouts = 0;// Tracksh ow many times in a row client failed to receive a chunk, so capped @ 10
    const int MAX_TRANSFER_TIMEOUTS = 10;// Max consecutive timeouts before we give up


    //THE LOOP BELOW IS MAIN IMAGE RECEPTION LOOP, KEEPS GOING UNTIL WE COMPLETE IMAGE OR WE TIMEOUT
    while(!transfer_complete && transfer_timeouts < MAX_TRANSFER_TIMEOUTS) {
        int relay_len = sizeof(relay_addr);
        int bytes_received= recvfrom(sock_fd, buffer, IMAGE_BUFFER_SIZE - 1, 0,(struct sockaddr*)&relay_addr,&relay_len);
//format -> socket name, buffer for data, max no. bytes, udp (if 0), struct for sender address, variable w/size of struct



        if (bytes_received > 0) { transfer_timeouts = 0;  // Got sth from the relay, resets timeout
            if (bytes_received < 20) { // Small message, might be the completion signal
                buffer[bytes_received] = '\0'; // Termination
                std::string response(buffer, bytes_received); //response stores data from buffer, first bytes_received chars

                if(response == "IMAGE_COMPLETE") { // Server says it's done sending, look at server side to see this
                    bool all_chunks_received = true;
                    if (total_chunks >= 0){ // Only if we know how many chunks to expect
                        for (int i = 0; i <= total_chunks;i++) { // is each chunk number there?
                            if (!chunk_received[i]) { // Found a missing chunk
                                all_chunks_received = false;
                                std::string retry_msg = "RETRY_CHUNK " + std::to_string(i);// Request missing chunk, "RETRY CHUNK " does that.
                                sendto(sock_fd, retry_msg.c_str(),retry_msg.length(), 0,(struct sockaddr*)&relay_addr, sizeof(relay_addr));
                                std::cout << "[RETRY] Requesting missing chunk " << i + 1 << "/" << (total_chunks + 1) << std::endl;
                                break;
                            }
                        }
                    }

//BELOW HERE IS JUST MORE CHECKS FOR RDT ETC
                    if (all_chunks_received && total_chunks >=0) { // Important to check total_chunks>=0 bcs it's -1 by default
                        sendto(sock_fd, "ACK_COMPLETE",12,0, (struct sockaddr*)&relay_addr, sizeof(relay_addr));
                        transfer_complete = true;
                        std::cout << "[INFO] Image transfer complete" << std::endl;
                        for (int i = 0; i <= total_chunks; i++) {
                                output_file.write(chunk_data[i].data(), chunk_data[i].size()); // Send to output file...
                        }
                    }
                    continue;
                }
            }

            std::string header_str(buffer,std::min(30, bytes_received)); // Get start of buffer (first 30 chars) as string
            //30 bytes is just a safe upper limit bcs our first part (CHUNK a/b) will always be under that length
            if (header_str.find("CHUNK ") == 0) { // Just checking if chunk...
                size_t header_end = 0; // tracking cur end of header, as you see it's increased later on until we find end
                for (size_t i = 6; i < bytes_received; i++) { // After "CHUNK "
                    if (buffer[i] == ' ') {  // Until we reach actual text we don't read in binary data
                        header_end = i + 1;
                        break;
                    }
                }

                if (header_end > 0) { // Kind of a check if there's data bcs it checks if there's space after CHUNK
                    std::string chunk_info = header_str.substr(6, header_end -7); // Extracts fractional completion
                    size_t slash_pos = chunk_info.find('/'); // Format is like "3/20" meaning chunk 3 of 20 total
                    if (slash_pos != std::string::npos) {
                        int chunk_num = std::stoi(chunk_info.substr(0, slash_pos)); // these stoi commands just turn first and second
                        total_chunks = std::stoi(chunk_info.substr(slash_pos + 1));//elements of fractions (i.e. 3/20) to ints

                        std::vector<char> data(buffer + header_end, buffer + bytes_received); // Ski pimage header + keep in binary file
                        chunk_data[chunk_num] = data; // Save in map by chunk number
                        chunk_received[chunk_num] = true;
                        std::cout << "[Received] Chunk " << chunk_num + 1 << "/" << total_chunks + 1<< std::endl;
                        std::string ack_msg = "ACK_CHUNK " + std::to_string(chunk_num); // ACK server!
                        sendto(sock_fd, ack_msg.c_str(), ack_msg.length(), 0,
                              (struct sockaddr*)&relay_addr, sizeof(relay_addr));
                    }
                }
            }
        } else {
            transfer_timeouts++; // Increment timeout counter
            if(total_chunks >= 0){
                bool missing_chunks = false;
                for (int i = 0; i <= total_chunks;i++) { // Look for any chunks we haven't received yet
                    if(!chunk_received[i]){
                        missing_chunks = true;
                        std::string retry_msg = "RETRY_CHUNK " + std::to_string(i); //Retry...
                        sendto(sock_fd, retry_msg.c_str(), retry_msg.length(), 0, (struct sockaddr*)&relay_addr, sizeof(relay_addr));
                        std::cout << "[RETRY] Requesting chunk " << i+ 1 << "/" << total_chunks + 1 << std::endl;
                        break;
                    }
                }

                if (!missing_chunks) { // if we didnt get completion msg
                    std::string complete_req = "REQ_COMPLETE"; // Trying to complete request w/server...
                    sendto(sock_fd, complete_req.c_str(), complete_req.length(), 0,(struct sockaddr*)&relay_addr, sizeof(relay_addr));
                    std::cout << "[RETRY] Asking for completion confirmation" << std::endl;
                }
            } else { // If we haven't gotten chunks yet...
                std::string retry_msg = "RETRY_CHUNK 0"; // Try to get things began
                sendto(sock_fd, retry_msg.c_str(), retry_msg.length(), 0,
                      (struct sockaddr*)&relay_addr, sizeof(relay_addr));
                std::cout << "[RETRY] Asking for first chunk" << std::endl;
            }
        }
    }

    if (!transfer_complete) { // Only happens if too many timeouts and EVERY other thing we've done has failed...
        std::cout << "[ERROR] Transfer timed out too many times, saving partial image" << std::endl;
        if (total_chunks >= 0) {
            for (int i = 0; i <= total_chunks; i++) { // The image will be incomplete but kind of viewable... just w/massive gaps
                if(chunk_received[i] && chunk_data.find(i) != chunk_data.end()) {
                    output_file.write(chunk_data[i].data(), chunk_data[i].size());
                }
            }
        }
    }

    output_file.close();
    std::cout << "[SUCCESS] saved as 'received_image.jpg'" << std::endl; closesocket(sock_fd);
}

void sendData(int port, std::string dataToSend) {
    SOCKET sock_fd;
    struct sockaddr_in relay_addr;
    char buffer[BUFFER_SIZE];

    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd == INVALID_SOCKET) {
        std::cerr << "[ERROR] Socket creation failed on port " << port << ": " << WSAGetLastError() << std::endl;
        return;
    }
    int timeout = TIMEOUT;
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    relay_addr.sin_family = AF_INET;
    relay_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, RELAY_IP, &relay_addr.sin_addr) <= 0){
        std::cerr << "[ERROR] Invalid address for relay\n";
        closesocket(sock_fd);
        return;
    }
    std::string packet = "SEQ " + std::to_string(portSeqNums[port]) + " | " + dataToSend;
    int retries = 0;
    bool acked = false;

    while (retries < MAX_RETRIES && !acked) { // Stop+wait
        sendto(sock_fd, packet.c_str(), packet.length(), 0, (struct sockaddr*)&relay_addr, sizeof(relay_addr));
        std::cout << "[Sent] " << packet << " on port " << port << std::endl;
        int relay_len = sizeof(relay_addr);
        int bytes_received = recvfrom(sock_fd, buffer, BUFFER_SIZE -1, 0,  (struct sockaddr*)&relay_addr, &relay_len);

        if (bytes_received > 0) { // Got a response?
            buffer[bytes_received] = '\0'; // NUll terminator to end
            std::string response(buffer);

            if(response.rfind("ACK " + std::to_string(portSeqNums[port]), 0) == 0) {// ACK verification like in above func
                std::cout << "[Server Response via Relay] " << response << std::endl;
                acked = true;
                portSeqNums[port] = 1 - portSeqNums[port]; // Flip sequence number @ this port
            } else{
                // Old/duplicate packet or wrong seq number?
                std::cout << "[IGNORED] Unexpected ACK or corrupted packet: " << response << std::endl;
            }
        } else{
            std::cout << "[TIMEOUT] No ACK received, retrying... (Attempt "<< retries + 1 << ")\n"; // Timeout msg
            retries++; // Still counted as retry tho
        }
    }

    if (!acked) std::cerr << "[ERROR] Max retries reached. No valid response from server.\n";//No ack after a bunch of retries
    closesocket(sock_fd);
}

// MUCH OF WHAT YOU SEE BELOW IS JUST SIMILAR LOGIC TO THE ABOVE FUNCTION SO ISN'T SUPER
//IMPORTANT TO COMMENT
void requestData(int port, std::string requestMessage) { // For sending normal data req to relay.
    SOCKET sock_fd;
    struct sockaddr_in relay_addr;
    char buffer[BUFFER_SIZE]; // This is just more of what we did above so I'd read above

    sock_fd = socket(AF_INET, SOCK_DGRAM, 0); // UDP socket created for this req
    if (sock_fd == INVALID_SOCKET) { // UDP is connectionless, so each request gets its own socket
        std::cerr << "[ERROR] Socket creation failed on port " << port << ": "<< WSAGetLastError() << std::endl;
        return;
    }
    int timeout = TIMEOUT;
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));


    relay_addr.sin_family = AF_INET;
    relay_addr.sin_port = htons(port); // Port -> network byte order, described in prev function for image transfer too
    if (inet_pton(AF_INET, RELAY_IP, &relay_addr.sin_addr) <= 0){ // Relay IP conversion..
        std::cerr << "[ERROR] Invalid address for relay\n"; // IP not valid?
        closesocket(sock_fd);
        return;
    }

    std::string packet = "SEQ " + std::to_string(portSeqNums[port]) + " | " + requestMessage;
    int retries = 0;
    bool acked = false;

    while (retries < MAX_RETRIES && !acked) { // Stop+wait
        sendto(sock_fd, packet.c_str(), packet.length(), 0, (struct sockaddr*)&relay_addr, sizeof(relay_addr));
        std::cout << "[Sent] " << packet << " on port " << port << std::endl;
        int relay_len = sizeof(relay_addr); // Length of address?
        int bytes_received = recvfrom(sock_fd, buffer, BUFFER_SIZE -1, 0,  (struct sockaddr*)&relay_addr, &relay_len);

        if (bytes_received > 0) { // Got a response?
            buffer[bytes_received] = '\0'; // NUll terminator to end
            std::string response(buffer);

            if(response.rfind("ACK " + std::to_string(portSeqNums[port]), 0) == 0) {// ACK verification like in above func
                std::cout << "[Server Response via Relay] " << response << std::endl;
                acked = true;
                portSeqNums[port] = 1 - portSeqNums[port]; // Flip sequence number @ this port
            } else{
                // Old/duplicate packet or wrong seq number?
                std::cout << "[IGNORED] Unexpected ACK or corrupted packet: " << response << std::endl;
            }
        } else{
            std::cout << "[TIMEOUT] No ACK received, retrying... (Attempt "<< retries + 1 << ")\n"; // Timeout msg
            retries++; // Still counted as retry tho
        }
    }

    if (!acked) std::cerr << "[ERROR] Max retries reached. No valid response from server.\n";//No ack after a bunch of retries
    closesocket(sock_fd);
}

int main() {
    WSADATA wsaData; // This inits winsock
    int wsaerr = WSAStartup(MAKEWORD(2, 2), &wsaData); // More winsock spamming
    std::cout << "Enter password: \n";
    std::string userPassword;
    std::getline(std::cin,userPassword);
    if (userPassword != "group_13") { // Basic authentication
        return 1;
    }

    // This is our earth/control center interface
    std::cout << "Rover ready to send telemetry data via relay. Available commands:\n";
    std::cout << "'rock' -> Request rock type\n";
    std::cout << "'temp' -> Request temperature\n";
    std::cout << "'monolith' -> Request monolith presence\n";
    std::cout << "'radiation' -> Request radiation levels\n";
    std::cout << "'image' -> Request lunar surface image\n";
    std::cout << "'degree, integer' -> Change direction of the buggy in degrees\n";
    std::cout << "'speed, integer' -> Change speed of buggy by m/s\n";
    std::cout << "'broadcast <message>' -> Broadcast message to all rovers in vicinity\n";
    std::cout << "'send <ip> <message>' -> Send direct message to a specific rover\n";

    while (true) { // Listen until user closes programme, here we process user commands, call functions etc
        std::string input;
        std::getline(std::cin, input); // and read...
        std::smatch matches;


        if (input == "rock") requestData(PORT_ROCK, "REQ ROCK_TYPE");
        else if (input == "temp") requestData(PORT_TEMP, "REQ TEMP");
        else if (input == "monolith") requestData(PORT_MONOLITH, "REQ MONOLITH");
        else if (input == "radiation") requestData(PORT_RADIATION, "REQ RADIATION");
        else if (input == "image") requestImage();
        else if (std::regex_match(input, matches, std::regex(R"(degree\s*,\s*(-?\d+))"))) sendData(PORT_DEGREE, matches[1].str());
        else if (std::regex_match(input, matches, std::regex(R"(speed\s*,\s*(-?\d+))"))) sendData(PORT_SPEED, matches[1].str());
        else if (input.substr(0, 9) == "broadcast" && input.length() > 10) {
            std::string message = input.substr(10); // Get message after "broadcast ", position 10
            sendRoverCommand("BROADCAST:" + message);
        }
        else if (input.substr(0, 4) == "send" && input.length() > 5) {
            std::istringstream iss(input.substr(5)); // Skip "send" if we are getting msg to specific rovers
            std::string ip;
            iss >> ip; // get IP
            std::string message; // Rest of string is msg
            std::getline(iss >> std::ws, message); // input
            if (!ip.empty() && !message.empty()) { // If valid IP
                sendRoverCommand("SEND:" + ip + ":" + message);
            } else {
                std::cout << "[ERROR] Send command format: send <ip> <message>\n";
            }
        }
        else std::cout << "[ERROR] Unknown command\n";
    }
}