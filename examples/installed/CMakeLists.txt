cmake_minimum_required(VERSION 3.20)
project(spwkit_installed_consumer LANGUAGES C)

include(CTest)
find_package(SpWKit 0.4 CONFIG REQUIRED)

if(NOT DEFINED SpWKit_UDP_RUNTIME_SUPPORTED)
    message(FATAL_ERROR "Installed SpWKit package does not declare UDP runtime availability")
endif()
if(NOT SpWKit_UDP_RUNTIME_SCOPE STREQUAL "POSIX")
    message(FATAL_ERROR "Unexpected SpWKit UDP runtime scope: ${SpWKit_UDP_RUNTIME_SCOPE}")
endif()

# This is deliberately a C-only project. spwkit::spwkit must compile and link
# without enabling CXX or forcing a C++ linker/runtime.
add_executable(spwkit_installed_consumer main.c)
target_link_libraries(spwkit_installed_consumer PRIVATE spwkit::spwkit)
if(SpWKit_UDP_RUNTIME_SUPPORTED)
    target_compile_definitions(spwkit_installed_consumer PRIVATE
        SPWKIT_INSTALLED_EXPECT_UDP_RUNTIME=1)
else()
    target_compile_definitions(spwkit_installed_consumer PRIVATE
        SPWKIT_INSTALLED_EXPECT_UDP_RUNTIME=0)
endif()
set_target_properties(spwkit_installed_consumer PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
)

add_test(NAME installed_package_consumer COMMAND spwkit_installed_consumer)
