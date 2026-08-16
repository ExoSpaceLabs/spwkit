cmake_minimum_required(VERSION 3.20)
project(spwkit_installed_device_c LANGUAGES C)

find_package(SpWKit 0.4 CONFIG REQUIRED)

if(NOT SpWKit_DEVICE_RUNTIME_SUPPORTED)
    message(FATAL_ERROR "Installed SpWKit package does not provide the Linux device runtime")
endif()
if(NOT SpWKit_DEVICE_RUNTIME_SCOPE STREQUAL "Linux")
    message(FATAL_ERROR "Unexpected SpWKit device runtime scope: ${SpWKit_DEVICE_RUNTIME_SCOPE}")
endif()
if(NOT TARGET spwkit::spwkit)
    message(FATAL_ERROR "Expected installed spwkit::spwkit target")
endif()

add_executable(spwkit_installed_device_c main.c)
target_link_libraries(spwkit_installed_device_c PRIVATE spwkit::spwkit)
set_target_properties(spwkit_installed_device_c PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF)
