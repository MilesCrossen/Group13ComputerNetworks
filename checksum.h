//CHECKSUM.H
// Simple error detection for lunar communications

#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <vector>
#include <string>
#include <cstdint>

// Calculate 16-bit Fletcher's checksum
inline uint16_t calculateChecksum(const void* data, size_t length) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;

    for (size_t i = 0; i < length; i++) {
        sum1 = (sum1 + bytes[i]) % 255;
        sum2 = (sum2 + sum1) % 255;
    }

    return (sum2 << 8) | sum1;
}

// Add checksum to a message
inline std::string addChecksum(const std::string& message) {
    uint16_t checksum = calculateChecksum(message.c_str(), message.length());
    return message + " | CHECKSUM:" + std::to_string(checksum);
}

// Verify checksum in a message
inline bool verifyChecksum(const std::string& message, std::string& originalMessage) {
    size_t pos = message.find(" | CHECKSUM:");
    if (pos == std::string::npos) {
        return false;
    }

    // Extract original message and checksum
    originalMessage = message.substr(0, pos);
    std::string checksumStr = message.substr(pos + 12); // Skip " | CHECKSUM:"

    // Calculate checksum of original message
    uint16_t calculatedChecksum = calculateChecksum(originalMessage.c_str(), originalMessage.length());
    uint16_t receivedChecksum = std::stoi(checksumStr);

    // Verify checksums match
    return (calculatedChecksum == receivedChecksum);
}

// Overload for when we don't need the original message
inline bool verifyChecksum(const std::string& message) {
    std::string dummy;
    return verifyChecksum(message, dummy);
}

#endif // CHECKS