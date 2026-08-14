cmake_minimum_required(VERSION 3.20)

project(
    spwkit
    VERSION 0.0.0
    DESCRIPTION "SpaceWire Development & Integration Toolkit"
    LANGUAGES C CXX
)

option(SPWKIT_BUILD_TESTS "Build SpWKit tests" ON)
option(SPWKIT_BUILD_EXAMPLES "Build SpWKit examples" ON)
option(SPWKIT_BUILD_SIMULATOR "Build the local virtual SpaceWire simulator backend" ON)
option(SPWKIT_BUILD_TOOLS "Build SpWKit command-line tools" OFF)

set(SPWKIT_SOURCES
    src/core/spwkit.cpp
    src/core/port.cpp
    src/backends/loopback/loopback_backend.cpp
)

if(SPWKIT_BUILD_SIMULATOR)
    list(APPEND SPWKIT_SOURCES
        src/backends/virtual/simulator_backend.cpp
    )
endif()

add_library(spwkit STATIC ${SPWKIT_SOURCES})

add_library(SpWKit::spwkit ALIAS spwkit)

target_compile_features(spwkit PUBLIC cxx_std_17)

target_include_directories(spwkit
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(SPWKIT_BUILD_SIMULATOR)
    target_compile_definitions(spwkit PRIVATE SPWKIT_HAS_SIMULATOR=1)
endif()

set_target_properties(spwkit PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if(SPWKIT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(SPWKIT_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
