//CLIENT.CPP

#include <iostream> // Standard input-output stream
#include <cstring> // C-style string functions
#include <winsock2.h>// Windows-specific socket programming
#include <ws2tcpip.h> // Additional TCP/IP utilities
#include <thread>// Multi-threading
#include <fstream> // For writing image files
#include <vector> // For storing image data
#include <map> // For tracking chunks
#pragma comment(lib, "ws2_32.lib") // Link Winsock library for networking
#define RELAY_IP "127.0.0.1" // Server IP
#define PORT_ROCK 5000 // Port spammm
#define PORT_TEMP 5001
#define PORT_MONOLITH 5002
#define PORT_RADIATION 5003
#define PORT_IMAGE 5004 // New port for image requests
#define BUFFER_SIZE 256 // This is all explained in other files tbh idk why I'd explain it again
#define MAX_RETRIES 5 // Retry count for lost packets
#define IMAGE_BUFFER_SIZE 2048 // Larger buffer for image data
#define IMAGE_TIMEOUT 5000 // 5 second timeout for image operations

// Keep track of sequence numbers for each port separately
//fixing the problem where requests on different ports would interfere with each other
// Each service (rock, temp, etc.) gets its own sequence number counter starting at 0
std::map<int, int> portSeqNums = { // Seq nums of each port are tracked separately
    {PORT_ROCK, 0},
    {PORT_TEMP, 0},
    {PORT_MONOLITH, 0},
    {PORT_RADIATION, 0},
    {PORT_IMAGE, 0}
};

void requestImage() {
    SOCKET sock_fd; // Make socket
    struct sockaddr_in relay_addr;
    char buffer[IMAGE_BUFFER_SIZE]; // Larger buffer for image data
    size_t image_size = 0; // Will store total size of image in bytes

    sock_fd = socket(AF_INET, SOCK_DGRAM, 0); // Create a UDP socket for image communication
    if (sock_fd == INVALID_SOCKET) { //-> our communication channel to relay satellite
        std::cerr << "[ERROR] Socket creation failed: " << WSAGetLastError() << std::endl;
        return;
    }

    int timeout = IMAGE_TIMEOUT;// Regular data timeout is 2000ms, but images need more time because they're larger
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    relay_addr.sin_family = AF_INET; // IPv4
    relay_addr.sin_port = htons(PORT_IMAGE); // Image port
    if (inet_pton(AF_INET, RELAY_IP, &relay_addr.sin_addr) <= 0) {
        std::cerr << "[ERROR] Invalid address for relay\n";
        closesocket(sock_fd);
        return; // This whole thing tells socket where to send data
    }

    // RDT 3.0 for initial image request -use port-specific sequence number
    // This is the stop-and-wait ARQ protocol (Automatic Repeat rEquest)
    // We send a packet, wait for ACK, send next packet
    std::string packet = "SEQ " + std::to_string(portSeqNums[PORT_IMAGE]) +" | REQ IMAGE";
    int retries = 0; // Counter for retry attempts
    bool received_size = false; // Flag for when we've got the image size


    while (retries < MAX_RETRIES && !received_size) { // Requesti mage + get size
        // Send req packet to the relay
        sendto(sock_fd, packet.c_str(), packet.length(), 0,
               (struct sockaddr*)&relay_addr, sizeof(relay_addr));
        std::cout << "[Sent] " << packet << " on port " << PORT_IMAGE << std::endl;

        int relay_len = sizeof(relay_addr);// Wait for a response (the image size information)
        int bytes_received = recvfrom(sock_fd, buffer, IMAGE_BUFFER_SIZE - 1, 0,
                                     (struct sockaddr*)&relay_addr, &relay_len);

        if (bytes_received > 0) { // Got a response, check if it's valid
            buffer[bytes_received] = '\0'; // Null-terminate for string operations
            std::string response(buffer);

            // Verify it's the right ACK (matches our sequence number) + has image size
            if (response.rfind("ACK " +std::to_string(portSeqNums[PORT_IMAGE]), 0)== 0 &&
                response.find("IMAGE_SIZE: ") != std::string::npos) {
                size_t pos = response.find("IMAGE_SIZE: ") + 12; // Extract image size
                std::string size_str = response.substr(pos);
                image_size = std::stoul(size_str); // Convert string to unsigned long
                std::cout << "[Server Response via Relay] Image size: " << image_size << " bytes" << std::endl;
                received_size = true; // Set flag that we got the size
                portSeqNums[PORT_IMAGE] = 1 - portSeqNums[PORT_IMAGE]; // Flip sequence number for this port only

                // Send acknowledgment that we got the size info
                // This tells the server it can start sending chunks
                sendto(sock_fd, "ACK_SIZE", 8, 0,
                      (struct sockaddr*)&relay_addr, sizeof(relay_addr));
            }else{
                // If the response doesn't match what we expect, ignore it
                // Could be out of order packet or response meant for someon eelse
                std::cout << "[IGNORED] Unexpected response: " << response << std::endl;
            }
        } else {
            // No response within timeout period? Go again
            std::cout << "[TIMEOUT] No response, retrying... (Attempt " << retries + 1 << ")\n";
            retries++;
        }
    }


    if (!received_size) { // If we got size info, proceed and if not we can't proceed
        std::cerr << "[ERROR] Failed to get image size" << std::endl;
        closesocket(sock_fd);
        return;
    }

    std::ofstream output_file("received_image.jpg", std::ios::binary); // Creating file to store
    if (!output_file) {//received image in binary
        std::cerr << "[ERROR] Failed to create output file" << std::endl;
        closesocket(sock_fd);
        return;
    }

    // Ready to start receiving the actual image data in chunks
    std::cout << "[INFO] Starting image transfer..." << std::endl;
    bool transfer_complete = false; // Flag for when we've got the whole image

    // We need these maps to track which chunks we've received
    // and to store their data until we can write them in order
    std::map<int, bool> chunk_received; // Tracks which chunks we've got (by chunk number)
    std::map<int, std::vector<char>> chunk_data; // Stores actual data for each chunk
    int total_chunks = -1;  // Will be updated when we receive first chunk


    int transfer_timeouts = 0;// Safety feature to prevent infinite loops if transfer stalls
    const int MAX_TRANSFER_TIMEOUTS = 10; // Max consecutive timeouts before we give up

    // Main image reception loop
    // This keeps going until either we have the complete image
    // or we timeout too many times consecutively
    while(!transfer_complete && transfer_timeouts < MAX_TRANSFER_TIMEOUTS) {
        // Wait to receive the next chunk from the relay
        int relay_len = sizeof(relay_addr);
        int bytes_received= recvfrom(sock_fd, buffer, IMAGE_BUFFER_SIZE - 1, 0,
                                     (struct sockaddr*)&relay_addr,
                                     &relay_len);

        if (bytes_received > 0) { // Got sth from the relay
            transfer_timeouts = 0; // Reset timeout counter on successful receive

            // First check if it's the completion message
            // Completion messages are small, so we can identify them by size
            if (bytes_received < 20) { // Small message, might be the completion signal
                buffer[bytes_received] = '\0'; // Make it a proper string
                std::string response(buffer, bytes_received);

                if (response == "IMAGE_COMPLETE") { // Server says it's done sending
                    // Before confirming, we check if we actually have all chunks
                    bool all_chunks_received = true;

                    if (total_chunks >= 0){ // Only if we know how many chunks to expect
                        // Check each chunk number from 0 to total_chunks
                        for (int i = 0; i <= total_chunks;i++) {
                            if (!chunk_received[i]) { // Found a missing chunk
                                all_chunks_received = false;

                                std::string retry_msg = "RETRY_CHUNK " + std::to_string(i);// Request the missing chunk, specifically by number
                                sendto(sock_fd, retry_msg.c_str(),retry_msg.length(), 0,
                                      (struct sockaddr*)&relay_addr, sizeof(relay_addr));
                                std::cout << "[RETRY] Requesting missing chunk " << i + 1 << "/" << (total_chunks + 1) << std::endl;
                                break; // Just request one at a time to avoid flooding
                            }
                        }
                    }

                    if (all_chunks_received && total_chunks >= 0) { // If we have all chunks and we know how many there should be
                        sendto(sock_fd, "ACK_COMPLETE", 12, 0, // Tell the server we're done and have everything
                              (struct sockaddr*)&relay_addr, sizeof(relay_addr));
                        transfer_complete = true; // Set flag that we're done
                        std::cout << "[INFO] Image transfer complete" << std::endl;

                        for (int i = 0; i <= total_chunks; i++) { // Now we can write all chunks to the file in the correct order.
                            if (chunk_data.find(i) != chunk_data.end()) { // This ensures the image isn't corrupted by out-of-order reception
                                output_file.write(chunk_data[i].data(), chunk_data[i].size());
                            }
                        }
                    }
                    continue; // Skip the rest of loop
                }
            }



            // If we get here, it must be a chunk of image data
            // Let's look for chunk header to identify it
            std::string header_str(buffer, std::min(30, bytes_received)); // Get start of buffer as string
            if (header_str.find("CHUNK ") == 0) { // Confirmed it's a chunk
                // Find where the header ends and the actual image data begins
                // Headers look like "CHUNK 3/21 " followed by binary data
                size_t header_end = 0;
                for (size_t i = 6; i < bytes_received; i++) {
                    if (buffer[i] == ' ') { // Space after chunk info
                        header_end = i + 1; // Position after the space
                        break;
                    }
                }

                if (header_end > 0) { // If we found the end of the header
                    std::string chunk_info = header_str.substr(6, header_end - 7); // Extract chunk number and total chunks info
                    size_t slash_pos = chunk_info.find('/'); // Format is like "3/20" meaning chunk 3 of 20 total
                    if (slash_pos != std::string::npos) {
                        int chunk_num = std::stoi(chunk_info.substr(0, slash_pos)); // Parse the chunk number and total chunks
                        total_chunks = std::stoi(chunk_info.substr(slash_pos + 1));

                        // Store the actual image data in memory
                        // Skip the header, just keep the binary image data
                        std::vector<char> data(buffer + header_end, buffer + bytes_received);
                        chunk_data[chunk_num] = data; // Save in our map by chunk number
                        chunk_received[chunk_num] = true; // Mark this chunk as received

                        std::cout << "[Received] Chunk " << chunk_num + 1 << "/" << total_chunks + 1 << std::endl;

                        // Send acknowledgment for this specific chunk
                        // This tells the server we got this chunk successfully
                        std::string ack_msg = "ACK_CHUNK " + std::to_string(chunk_num);
                        sendto(sock_fd, ack_msg.c_str(), ack_msg.length(), 0,
                              (struct sockaddr*)&relay_addr, sizeof(relay_addr));
                    }
                }
            }
        } else {
            // No data received before timeout - packet loss???
            transfer_timeouts++; // Increment timeout counter

            if (total_chunks >= 0) { // If we know how many chunks to expect, we can check for missing ones
                bool missing_chunks = false;


                for (int i = 0; i <= total_chunks; i++) { // Look for any chunks we haven't received yet
                    if (!chunk_received[i]) {
                        missing_chunks = true;

                        std::string retry_msg = "RETRY_CHUNK " + std::to_string(i); // Request this specific missing chunk
                        sendto(sock_fd, retry_msg.c_str(), retry_msg.length(), 0,
                              (struct sockaddr*)&relay_addr, sizeof(relay_addr));
                        std::cout << "[RETRY] Requesting chunk " << i + 1 << "/" << total_chunks + 1 << std::endl;

                        break; // Only request one chunk at a time to avoid flooding the network
                    }
                }

                // If we've received all chunks but didn't get the completion msg
                if (!missing_chunks) { // (maybe it got lost), explicitly request it
                    std::string complete_req = "REQ_COMPLETE";
                    sendto(sock_fd, complete_req.c_str(), complete_req.length(), 0,
                          (struct sockaddr*)&relay_addr, sizeof(relay_addr));
                    std::cout << "[RETRY] Requesting completion confirmation" << std::endl;
                }
            } else { // If we haven't received any chunks yet (or don't know total_chunks)
                std::string retry_msg = "RETRY_CHUNK 0"; // request the first chunk to get things started
                sendto(sock_fd, retry_msg.c_str(), retry_msg.length(), 0,
                      (struct sockaddr*)&relay_addr, sizeof(relay_addr));
                std::cout << "[RETRY] Requesting first chunk" << std::endl;
            }
        }
    }

    if (!transfer_complete) { // If we get here without completing the transfer, it means we had too many timeouts
        std::cout << "[ERROR] Transfer timed out too many times, saving partial image" << std::endl;

        if (total_chunks >= 0) {// Save whatever chunks we did manage to receive
            for (int i = 0; i <= total_chunks; i++) { // The image will be incomplete but someone viewable... just w/massive gaps
                if (chunk_received[i] && chunk_data.find(i) != chunk_data.end()) {
                    output_file.write(chunk_data[i].data(), chunk_data[i].size());
                }
            }
        }
    }

    output_file.close(); // Cleanup...
    std::cout << "[SUCCESS] Image saved as 'received_image.jpg'" << std::endl;

    closesocket(sock_fd); // Close socket
}

void requestData(int port, std::string requestMessage) { // Send req to relay
    SOCKET sock_fd; // Socket file descriptor - our connection handle
    struct sockaddr_in relay_addr; // For relay address
    char buffer[BUFFER_SIZE]; // Incoming data..

    sock_fd = socket(AF_INET, SOCK_DGRAM, 0); // UDP socket created for this req
    if (sock_fd == INVALID_SOCKET) { // UDP is connectionless, so each request gets its own socket
        std::cerr << "[ERROR] Socket creation failed on port " << port << ": "<< WSAGetLastError() << std::endl;
        return;
    }

    // Set a timeout so we don't wait forever if a packet gets lost
    // This is shorter than the image timeout since these are small msgs
    int timeout = 2000; // 2 second receive timeout
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));


    relay_addr.sin_family = AF_INET; // Configure the address we send to (the relay satellite)/IPv4
    relay_addr.sin_port = htons(port); // Convert port to network byte order, done in relay too
    if (inet_pton(AF_INET, RELAY_IP, &relay_addr.sin_addr) <= 0){
        std::cerr << "[ERROR] Invalid address for relay\n"; // Is IP valid?
        closesocket(sock_fd);
        return;
    }

    // Create the packet with sequence number and request type. Sequence part implemetns rdt protocol
    std::string packet = "SEQ " + std::to_string(portSeqNums[port]) + " | " + requestMessage; // Append seq num to msg
    int retries = 0; // How many times we tried
    bool acked = false; // Have we gotten the right ACK?



    while (retries < MAX_RETRIES && !acked) { // Stop-and-wait loop i.e. keep sending until we get ACK or run outta retries
        sendto(sock_fd, packet.c_str(), packet.length(), 0, // Send req packet to relay
               (struct sockaddr*)&relay_addr, sizeof(relay_addr));
        std::cout << "[Sent] " << packet << " on port " << port << std::endl; // Print out


        // Wait for a response (hopefully an ACK)


        int relay_len = sizeof(relay_addr); // Length of address
        int bytes_received = recvfrom(sock_fd, buffer, BUFFER_SIZE - 1,
            0,  (struct sockaddr*)&relay_addr, &relay_len);

        if (bytes_received > 0) { // If we received a response
            buffer[bytes_received] = '\0'; //Terminate...
            std::string response(buffer);

            if (response.rfind("ACK " + std::to_string(portSeqNums[port]), 0) == 0) {// Here we verify
                std::cout << "[Server Response via Relay] " << response << std::endl; //that sequence number matches
                acked = true; // Got it! We can stop retrying
                portSeqNums[port] = 1 - portSeqNums[port]; // Flip sequence number for this port only
            } else{
                // Response doesn't match what we expected
                // Could be duplicate/old packet or wrong sequence number
                std::cout << "[IGNORED] Unexpected ACK or corrupted packet: " << response << std::endl; // Ignore
            }
        } else {
            std::cout << "[TIMEOUT] No ACK received, retrying... (Attempt "<< retries + 1 << ")\n"; // Timeout msg
            // No response received within timeout period
            retries++; // Count this retry
        }
    }

    if (!acked) std::cerr << "[ERROR] Max retries reached. No valid response from server.\n"; // Max retries fail
    // If we used all our retries and still didn't get an ACK, give up

    closesocket(sock_fd); // Close socket at end
}

int main() {
    WSADATA wsaData; // Initialize Winsock - library used for Windows socket programming
    int wsaerr = WSAStartup(MAKEWORD(2, 2), &wsaData); // More winsock spamming
    if (wsaerr != 0) { //If error
        std::cerr << "WSAStartup failed: " << wsaerr << std::endl;
        return 1;
    }

    // Show the command menu to the user
    // This is our earth/control center interface
    std::cout << "Rover ready to send telemetry data via relay. Available commands:\n";
    std::cout << "'rock' -> Request rock type\n";
    std::cout << "'temp' -> Request temperature\n";
    std::cout << "'monolith' -> Request monolith presence\n";
    std::cout << "'radiation' -> Request radiation levels\n";
    std::cout << "'image' -> Request lunar surface image\n";


    while (true) { // Listen until user closes programme, here we process user commands, call functions etc
        std::string input;
        std::getline(std::cin, input); // and read...

        if (input == "rock") requestData(PORT_ROCK, "REQ ROCK_TYPE"); // Rock type
        else if (input == "temp") requestData(PORT_TEMP, "REQ TEMP"); // Request temperature
        else if (input == "monolith") requestData(PORT_MONOLITH, "REQ MONOLITH");// Check for monolith presence
        else if (input == "radiation") requestData(PORT_RADIATION, "REQ RADIATION"); // Get radiation level
        else if (input == "image") requestImage(); // Request lunar surface image
        else std::cout << "[ERROR] Unknown command\n"; // If not recognised, error
    }

    WSACleanup(); // Cleanup which doesn't actually execute bcs we have infinite loop, but it's there
    return 0; //just because
}