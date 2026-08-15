cmake_minimum_required(VERSION 3.20)

project(
    spwkit
    VERSION 0.1.0
    DESCRIPTION "SpaceWire Development & Integration Toolkit"
    LANGUAGES C CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(SPWKIT_BUILD_TESTS "Build SpWKit tests" ON)
option(SPWKIT_BUILD_EXAMPLES "Build SpWKit examples" ON)
option(SPWKIT_BUILD_SIMULATOR "Build the local virtual SpaceWire simulator backend" ON)
option(SPWKIT_BUILD_UDP "Build the distributed VSPW-TP/UDP backend" ON)
option(SPWKIT_BUILD_TOOLS "Build SpWKit command-line tools" OFF)
option(SPWKIT_ENABLE_HEAP "Enable heap-backed spw_port_open convenience API" ON)

# Apply the same exception/RTTI policy to library code and any internal C++
# test that directly instantiates implementation classes. Public consumers are
# not forced to use these flags.
function(spwkit_apply_no_throw_policy target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /EHs-c- /GR-)
    else()
        target_compile_options(${target_name} PRIVATE -fno-exceptions -fno-rtti)
    endif()
endfunction()

set(SPWKIT_SOURCES
    src/core/spwkit.cpp
    src/core/port.cpp
    src/backends/loopback/loopback_backend.cpp
    src/backends/ethernet/vspw_tp.cpp
)

if(SPWKIT_BUILD_SIMULATOR)
    list(APPEND SPWKIT_SOURCES
        src/backends/virtual/simulator_backend.cpp
        src/backends/virtual/simulator_zero_copy.cpp
    )
endif()

if(SPWKIT_BUILD_UDP AND UNIX)
    list(APPEND SPWKIT_SOURCES
        src/backends/ethernet/udp_backend.cpp
    )
endif()

add_library(spwkit STATIC ${SPWKIT_SOURCES})
add_library(SpWKit::spwkit ALIAS spwkit)

target_compile_features(spwkit PUBLIC cxx_std_17)

target_include_directories(spwkit
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

spwkit_apply_no_throw_policy(spwkit)

if(SPWKIT_BUILD_SIMULATOR)
    target_compile_definitions(spwkit PRIVATE SPWKIT_HAS_SIMULATOR=1)
endif()

if(SPWKIT_BUILD_UDP AND UNIX)
    target_compile_definitions(spwkit PRIVATE SPWKIT_HAS_UDP=1)
endif()

if(SPWKIT_ENABLE_HEAP)
    target_compile_definitions(spwkit PRIVATE SPWKIT_ENABLE_HEAP=1)
else()
    target_compile_definitions(spwkit PRIVATE SPWKIT_ENABLE_HEAP=0)
endif()

set_target_properties(spwkit PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    EXPORT_NAME spwkit
)

if(SPWKIT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(SPWKIT_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

install(
    TARGETS spwkit
    EXPORT SpWKitTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    DIRECTORY include/spwkit
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpWKitConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/SpWKitConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SpWKit
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/SpWKitConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    EXPORT SpWKitTargets
    FILE SpWKitTargets.cmake
    NAMESPACE SpWKit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SpWKit
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/SpWKitConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/SpWKitConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SpWKit
)
