cmake_minimum_required(VERSION 3.20)
project(spwkit_installed_consumer LANGUAGES C CXX)

include(CTest)
find_package(SpWKit 0.1 CONFIG REQUIRED)

# The public API consumed below is C. The v0.1 static archive is implemented in
# C++, so the consumer toolchain also enables CXX to provide the C++ runtime at
# final link. No C++ types or headers are required by application source.
add_executable(spwkit_installed_consumer main.c)
target_link_libraries(spwkit_installed_consumer PRIVATE SpWKit::spwkit)
set_target_properties(spwkit_installed_consumer PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    LINKER_LANGUAGE CXX
)

add_test(NAME installed_package_consumer COMMAND spwkit_installed_consumer)
