cmake_minimum_required(VERSION 3.20)

project(
    spwkit
    VERSION 0.0.0
    DESCRIPTION "SpaceWire Development & Integration Toolkit"
    LANGUAGES C CXX
)

option(SPWKIT_BUILD_TESTS "Build SpWKit tests" ON)
option(SPWKIT_BUILD_EXAMPLES "Build SpWKit examples" ON)
option(SPWKIT_BUILD_SIMULATOR "Build the virtual SpaceWire simulator" OFF)
option(SPWKIT_BUILD_TOOLS "Build SpWKit command-line tools" OFF)

add_library(spwkit STATIC
    src/core/spwkit.cpp
)

add_library(SpWKit::spwkit ALIAS spwkit)

target_compile_features(spwkit PUBLIC cxx_std_17)

target_include_directories(spwkit
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

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

# Simulator and tool targets are introduced after the public API and virtual
# transport contracts are stabilized. The options are reserved now so build
# configuration does not have to be renamed later.
