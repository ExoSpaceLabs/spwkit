cmake_minimum_required(VERSION 3.20)
project(spwkit_distributed_peer LANGUAGES C)

if(NOT UNIX)
    message(FATAL_ERROR "The VSPW-TP/UDP runtime backend currently requires a POSIX host")
endif()

find_package(SpWKit 0.6 CONFIG REQUIRED)

if(NOT SpWKit_UDP_RUNTIME_SUPPORTED)
    message(FATAL_ERROR "Installed SpWKit package does not provide the UDP runtime")
endif()

add_executable(spwkit_udp_peer udp_peer.c)
target_link_libraries(spwkit_udp_peer PRIVATE spwkit::spwkit)
set_target_properties(spwkit_udp_peer PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
)