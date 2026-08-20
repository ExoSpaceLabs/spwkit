cmake_minimum_required(VERSION 3.20)
project(spwkit_windows_udp_fixture LANGUAGES C)

find_package(SpWKit 0.5 REQUIRED)

if(NOT SpWKit_UDP_RUNTIME_SUPPORTED)
    message(FATAL_ERROR "Installed SpWKit package does not advertise UDP runtime support")
endif()
if(NOT SpWKit_UDP_RUNTIME_SCOPE STREQUAL "Winsock")
    message(FATAL_ERROR
        "Expected installed UDP runtime scope Winsock, got '${SpWKit_UDP_RUNTIME_SCOPE}'")
endif()

add_executable(spwkit_windows_udp_peer peer.c)
target_link_libraries(spwkit_windows_udp_peer PRIVATE spwkit::spwkit)
target_compile_features(spwkit_windows_udp_peer PRIVATE c_std_11)
set_target_properties(spwkit_windows_udp_peer PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF)
