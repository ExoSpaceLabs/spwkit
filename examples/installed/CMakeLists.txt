cmake_minimum_required(VERSION 3.20)
project(spwkit_installed_consumer LANGUAGES C)

include(CTest)
find_package(SpWKit 0.1 CONFIG REQUIRED)

add_executable(spwkit_installed_consumer main.c)
target_link_libraries(spwkit_installed_consumer PRIVATE SpWKit::spwkit)
set_target_properties(spwkit_installed_consumer PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
)

add_test(NAME installed_package_consumer COMMAND spwkit_installed_consumer)
