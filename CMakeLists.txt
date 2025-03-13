cmake_minimum_required(VERSION 3.28)
project(ComputerNetworks)

set(CMAKE_CXX_STANDARD 17)

# Create separate executables for server and client
add_executable(server server.cpp)
add_executable(client client.cpp)

# Link Winsock2 library to both (for Windows networking)
target_link_libraries(server ws2_32)
target_link_libraries(client ws2_32)