cmake_minimum_required(VERSION 3.20)

project(spwkit_profile_benchmark LANGUAGES C)

set(SPWKIT_BENCHMARK_PROFILE_START "SPW_PROFILE_ID_TX_API_ENTRY" CACHE STRING
    "Start profiling probe for the hosted benchmark")
set(SPWKIT_BENCHMARK_PROFILE_END "SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY" CACHE STRING
    "End profiling probe for the hosted benchmark")
set(SPWKIT_BENCHMARK_COUNTER_HZ "0" CACHE STRING
    "Optional architectural counter frequency override in Hz; 0 means unknown/platform-reported")
option(SPWKIT_BENCHMARK_BUILD_SIMULATOR
    "Build the process-local simulator for host backend benchmark cases" OFF)
option(SPWKIT_BENCHMARK_BUILD_UDP
    "Build the VSPW-TP/UDP runtime for UDP benchmark cases" OFF)
option(SPWKIT_BENCHMARK_BUILD_DEVICE
    "Build the Linux DEVICE/VSPD runtime and daemon for DEVICE benchmark cases" OFF)

set(SPWKIT_ENABLE_PROFILING ON CACHE BOOL "" FORCE)
set(SPWKIT_PROFILE_START "${SPWKIT_BENCHMARK_PROFILE_START}" CACHE STRING "" FORCE)
set(SPWKIT_PROFILE_END "${SPWKIT_BENCHMARK_PROFILE_END}" CACHE STRING "" FORCE)
set(SPWKIT_ENABLE_HEAP OFF CACHE BOOL "" FORCE)
set(SPWKIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPWKIT_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)
set(SPWKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPWKIT_BUILD_CPP_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPWKIT_BUILD_SIMULATOR ${SPWKIT_BENCHMARK_BUILD_SIMULATOR} CACHE BOOL "" FORCE)
set(SPWKIT_BUILD_UDP ${SPWKIT_BENCHMARK_BUILD_UDP} CACHE BOOL "" FORCE)
set(SPWKIT_BUILD_DEVICE ${SPWKIT_BENCHMARK_BUILD_DEVICE} CACHE BOOL "" FORCE)
set(SPWKIT_BUILD_VSPWD ${SPWKIT_BENCHMARK_BUILD_DEVICE} CACHE BOOL "" FORCE)
set(SPWKIT_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(SPWKIT_BUILD_CUSE OFF CACHE BOOL "" FORCE)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." spwkit-runtime)

if(NOT SPWKIT_BENCHMARK_COUNTER_HZ STREQUAL "0")
    target_compile_definitions(spwkit PRIVATE
        SPWKIT_PROFILE_COUNTER_HZ=${SPWKIT_BENCHMARK_COUNTER_HZ})
endif()

function(spwkit_configure_profile_executable target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE spwkit::spwkit)
    target_include_directories(${target_name} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../src")
    target_compile_definitions(${target_name} PRIVATE
        SPWKIT_ENABLE_PROFILING=1
        SPWKIT_PROFILE_START=${SPWKIT_BENCHMARK_PROFILE_START}
        SPWKIT_PROFILE_END=${SPWKIT_BENCHMARK_PROFILE_END}
    )
    if(NOT SPWKIT_BENCHMARK_COUNTER_HZ STREQUAL "0")
        target_compile_definitions(${target_name} PRIVATE
            SPWKIT_PROFILE_COUNTER_HZ=${SPWKIT_BENCHMARK_COUNTER_HZ})
    endif()
    target_compile_features(${target_name} PRIVATE c_std_11)
    set_target_properties(${target_name} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS OFF)
endfunction()

spwkit_configure_profile_executable(
    spwkit_profile_benchmark
    profile_benchmark.c)
spwkit_configure_profile_executable(
    spwkit_profile_counter_floor
    profile_counter_floor.c)
spwkit_configure_profile_executable(
    spwkit_profile_native_comparison
    profile_native_comparison.c)
spwkit_configure_profile_executable(
    spwkit_profile_native_receive_comparison
    profile_native_receive_comparison.c)
spwkit_configure_profile_executable(
    spwkit_profile_zero_copy_comparison
    profile_zero_copy_comparison.c)
spwkit_configure_profile_executable(
    spwkit_profile_host_backend
    profile_host_backend.c)

if(UNIX)
    spwkit_configure_profile_executable(
        spwkit_profile_udp_comparison
        profile_udp_comparison.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SPWKIT_BENCHMARK_BUILD_DEVICE)
    spwkit_configure_profile_executable(
        spwkit_profile_device_comparison
        profile_device_comparison.c)
endif()
