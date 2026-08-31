cmake_minimum_required(VERSION 3.20)
project(spwkit_distributed_cpp_peer LANGUAGES CXX)

if(NOT UNIX)
    message(FATAL_ERROR "The VSPW-TP/UDP runtime backend currently requires a POSIX host")
endif()

find_package(SpWKit 0.6 CONFIG REQUIRED)

if(NOT SpWKit_UDP_RUNTIME_SUPPORTED)
    message(FATAL_ERROR "Installed SpWKit package does not provide the UDP runtime")
endif()
if(NOT TARGET spwkit::cpp)
    message(FATAL_ERROR "Installed SpWKit package does not provide the optional C++ wrapper")
endif()

add_executable(spwkit_udp_peer_cpp udp_peer.cpp)
target_link_libraries(spwkit_udp_peer_cpp PRIVATE spwkit::cpp)
target_compile_features(spwkit_udp_peer_cpp PRIVATE cxx_std_17)
set_target_properties(spwkit_udp_peer_cpp PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)