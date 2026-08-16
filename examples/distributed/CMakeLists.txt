cmake_minimum_required(VERSION 3.20)
project(spwkit_distributed_peer LANGUAGES C CXX)

if(NOT UNIX)
    message(FATAL_ERROR "The v0.2 VSPW-TP/UDP runtime backend currently requires a POSIX host")
endif()

find_package(SpWKit 0.2 CONFIG REQUIRED)

add_executable(spwkit_udp_peer udp_peer.c)
target_link_libraries(spwkit_udp_peer PRIVATE SpWKit::spwkit)
set_target_properties(spwkit_udp_peer PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    LINKER_LANGUAGE CXX
)
