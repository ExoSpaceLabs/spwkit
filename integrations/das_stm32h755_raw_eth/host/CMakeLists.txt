cmake_minimum_required(VERSION 3.20)
project(spwkit_das_host_peer LANGUAGES C)

find_package(SpWKit CONFIG REQUIRED)

add_executable(spwkit_das_host_peer peer.c)
target_link_libraries(spwkit_das_host_peer PRIVATE spwkit::spwkit)
target_compile_features(spwkit_das_host_peer PRIVATE c_std_11)
target_compile_options(spwkit_das_host_peer PRIVATE -Wall -Wextra -Werror)
