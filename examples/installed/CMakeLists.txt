cmake_minimum_required(VERSION 3.20)
project(spwkit_installed_consumer LANGUAGES C)

include(CTest)
find_package(SpWKit 0.5 CONFIG REQUIRED)

if(NOT DEFINED SpWKit_UDP_RUNTIME_SUPPORTED)
    message(FATAL_ERROR "Installed SpWKit package does not declare UDP runtime availability")
endif()
if(WIN32)
    set(_spwkit_expected_udp_runtime_scope "Winsock")
else()
    set(_spwkit_expected_udp_runtime_scope "POSIX")
endif()
if(NOT SpWKit_UDP_RUNTIME_SCOPE STREQUAL _spwkit_expected_udp_runtime_scope)
    message(FATAL_ERROR
        "Unexpected SpWKit UDP runtime scope: ${SpWKit_UDP_RUNTIME_SCOPE}; "
        "expected ${_spwkit_expected_udp_runtime_scope}")
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
