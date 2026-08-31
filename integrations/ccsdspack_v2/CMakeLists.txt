cmake_minimum_required(VERSION 3.20)
project(spwkit_ccsdspack_v2_integration LANGUAGES CXX)

find_package(SpWKit 0.5 CONFIG REQUIRED)
find_package(CCSDSPack 2.0 CONFIG REQUIRED)

if(NOT TARGET spwkit::cpp)
    message(FATAL_ERROR "Installed SpWKit package does not provide the optional C++ wrapper")
endif()
if(NOT TARGET ccsdspack::CCSDSPack)
    message(FATAL_ERROR "Installed CCSDSPack package does not provide ccsdspack::CCSDSPack")
endif()
if(NOT SpWKit_UDP_RUNTIME_SUPPORTED)
    message(FATAL_ERROR "Installed SpWKit package does not provide the hosted UDP runtime")
endif()
if(NOT SpWKit_DEVICE_RUNTIME_SUPPORTED)
    message(FATAL_ERROR "Installed SpWKit package does not provide the Linux DEVICE runtime")
endif()

add_executable(spwkit_ccsdspack_v2_peer peer.cpp)
target_link_libraries(spwkit_ccsdspack_v2_peer
    PRIVATE
        spwkit::cpp
        ccsdspack::CCSDSPack
)
target_compile_features(spwkit_ccsdspack_v2_peer PRIVATE cxx_std_17)
set_target_properties(spwkit_ccsdspack_v2_peer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)
