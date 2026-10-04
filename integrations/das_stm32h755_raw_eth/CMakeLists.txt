cmake_minimum_required(VERSION 3.20)
project(spwkit_das_stm32h755_raw_eth LANGUAGES C)

find_package(SpWKit CONFIG REQUIRED)
find_package(DAS 0.1 CONFIG REQUIRED)

set(SPWKIT_DAS_HOST_MAC "" CACHE STRING
    "Host Ethernet MAC address in aa:bb:cc:dd:ee:ff form")
if(NOT SPWKIT_DAS_HOST_MAC MATCHES
   "^[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]$")
    message(FATAL_ERROR
        "SPWKIT_DAS_HOST_MAC must be aa:bb:cc:dd:ee:ff")
endif()

string(REPLACE ":" ";" _host_mac "${SPWKIT_DAS_HOST_MAC}")
foreach(_index RANGE 0 5)
    list(GET _host_mac ${_index} _octet)
    string(TOUPPER "${_octet}" _octet)
    set(MAC${_index} "0x${_octet}u")
endforeach()
configure_file(peer_config.h.in
               "${CMAKE_CURRENT_BINARY_DIR}/peer_config.h" @ONLY)

add_executable(spwkit_das_stm32h755_raw_eth
    firmware.c
    das_raw_io.c)
target_link_libraries(spwkit_das_stm32h755_raw_eth PRIVATE
    spwkit::spwkit
    das::das)
target_include_directories(spwkit_das_stm32h755_raw_eth PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(spwkit_das_stm32h755_raw_eth PRIVATE c_std_11)
target_compile_options(spwkit_das_stm32h755_raw_eth PRIVATE
    -Wall -Wextra -Werror -ffreestanding -fno-builtin -g3)
target_link_options(spwkit_das_stm32h755_raw_eth PRIVATE
    -nostartfiles
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--unresolved-symbols=report-all
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/spwkit_das_stm32h755_raw_eth.map)
set_target_properties(spwkit_das_stm32h755_raw_eth PROPERTIES SUFFIX ".elf")
