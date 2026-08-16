cmake_minimum_required(VERSION 3.20)
project(spwkit_installed_cpp_consumer LANGUAGES CXX)

include(CTest)
find_package(SpWKit 0.3 CONFIG REQUIRED)

if(NOT SpWKit_CPP_WRAPPER_AVAILABLE)
    message(FATAL_ERROR "Installed SpWKit package does not contain the optional C++ wrapper")
endif()
if(NOT TARGET spwkit::cpp)
    message(FATAL_ERROR "Expected installed spwkit::cpp target")
endif()

add_executable(spwkit_installed_cpp_consumer main.cpp)
target_link_libraries(spwkit_installed_cpp_consumer PRIVATE spwkit::cpp)
target_compile_features(spwkit_installed_cpp_consumer PRIVATE cxx_std_17)

add_test(NAME installed_cpp_package_consumer COMMAND spwkit_installed_cpp_consumer)
