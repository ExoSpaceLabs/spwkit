cmake_minimum_required(VERSION 3.20)
project(spwkit_v1_consumer LANGUAGES C)

find_package(SpWKit CONFIG REQUIRED)

add_executable(spwkit_v1_consumer main.c)
target_link_libraries(spwkit_v1_consumer PRIVATE spwkit::spwkit)
target_compile_features(spwkit_v1_consumer PRIVATE c_std_11)
set_target_properties(spwkit_v1_consumer PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF)
