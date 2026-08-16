cmake_minimum_required(VERSION 3.20)

# The implementation baseline is C. C++ is enabled only when a selected
# component still needs it during the v0.3 migration. The simulator is C11;
# UDP remains the final runtime C++ dependency until #48 lands.
project(
    spwkit
    VERSION 0.2.0
    DESCRIPTION "SpaceWire Development & Integration Toolkit"
    LANGUAGES C
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(SPWKIT_BUILD_TESTS "Build SpWKit tests" ON)
option(SPWKIT_BUILD_EXAMPLES "Build SpWKit examples" ON)
option(SPWKIT_BUILD_SIMULATOR "Build the local virtual SpaceWire simulator backend" ON)
option(SPWKIT_BUILD_UDP "Build the distributed VSPW-TP/UDP backend when supported" ON)
option(SPWKIT_BUILD_TOOLS "Build SpWKit command-line tools" OFF)
option(SPWKIT_ENABLE_HEAP "Enable heap-backed spw_port_open convenience API" ON)

if(SPWKIT_BUILD_UDP AND UNIX)
    set(SPWKIT_UDP_RUNTIME_SUPPORTED ON)
else()
    set(SPWKIT_UDP_RUNTIME_SUPPORTED OFF)
endif()

if(SPWKIT_BUILD_UDP AND NOT UNIX)
    message(STATUS
        "SpWKit UDP runtime is currently POSIX-only; public UDP API will be installed, "
        "but selecting SPW_BACKEND_UDP on this platform returns SPW_ERR_UNSUPPORTED")
endif()

# Core, loopback and simulator are C11. CXX is enabled only for the remaining
# C++ UDP runtime or for C++ tests/examples during the migration.
set(SPWKIT_NEEDS_CXX OFF)
if(SPWKIT_UDP_RUNTIME_SUPPORTED OR SPWKIT_BUILD_TESTS OR SPWKIT_BUILD_EXAMPLES)
    set(SPWKIT_NEEDS_CXX ON)
endif()
if(SPWKIT_NEEDS_CXX)
    enable_language(CXX)
endif()

function(spwkit_apply_no_throw_policy target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:/EHs-c->
            $<$<COMPILE_LANGUAGE:CXX>:/GR->)
    else()
        target_compile_options(${target_name} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
            $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
    endif()
endfunction()

set(SPWKIT_SOURCES
    src/core/port.c
    src/backends/loopback/loopback_backend.c
)

if(SPWKIT_BUILD_SIMULATOR)
    list(APPEND SPWKIT_SOURCES
        src/platform/host_sync.c
        src/backends/virtual/simulator_backend.c
        src/backends/virtual/simulator_factory_compat.c
    )
endif()

if(SPWKIT_UDP_RUNTIME_SUPPORTED)
    list(APPEND SPWKIT_SOURCES
        src/backends/ethernet/vspw_tp.cpp
        src/backends/ethernet/udp_backend.cpp
        src/core/cpp_backend_bridge.cpp
    )
elseif(SPWKIT_BUILD_TESTS)
    # Internal codec tests still exercise the v0.2 C++ VSPW-TP implementation
    # until #48 moves it into the C runtime.
    list(APPEND SPWKIT_SOURCES src/backends/ethernet/vspw_tp.cpp)
endif()

add_library(spwkit ${SPWKIT_SOURCES})
add_library(SpWKit::spwkit ALIAS spwkit)

target_compile_features(spwkit PRIVATE c_std_11)
if(SPWKIT_NEEDS_CXX)
    target_compile_features(spwkit PRIVATE cxx_std_17)
endif()

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
    if(UNIX)
        find_package(Threads REQUIRED)
        target_link_libraries(spwkit PRIVATE Threads::Threads)
    endif()
endif()

if(SPWKIT_UDP_RUNTIME_SUPPORTED)
    target_compile_definitions(spwkit PRIVATE SPWKIT_HAS_UDP=1)
endif()

if(SPWKIT_ENABLE_HEAP)
    target_compile_definitions(spwkit PRIVATE SPWKIT_ENABLE_HEAP=1)
else()
    target_compile_definitions(spwkit PRIVATE SPWKIT_ENABLE_HEAP=0)
endif()

set_target_properties(spwkit PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    EXPORT_NAME spwkit
)
if(SPWKIT_NEEDS_CXX)
    set_target_properties(spwkit PROPERTIES CXX_EXTENSIONS OFF)
endif()

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
