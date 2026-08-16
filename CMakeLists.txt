cmake_minimum_required(VERSION 3.20)

# libspwkit is a C11 runtime. C++ is an optional convenience layer above the
# public C ABI and is never required by the runtime implementation.
project(
    spwkit
    VERSION 0.4.0
    DESCRIPTION "SpaceWire Development & Integration Toolkit"
    LANGUAGES C
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(SPWKIT_BUILD_TESTS "Build SpWKit tests" ON)
option(SPWKIT_BUILD_CPP_TESTS "Build C++ test fixtures/wrapper tests" ON)
option(SPWKIT_BUILD_EXAMPLES "Build SpWKit examples" ON)
option(SPWKIT_BUILD_CPP_EXAMPLES "Build C++ examples" ON)
option(SPWKIT_BUILD_SIMULATOR "Build the local virtual SpaceWire simulator backend" ON)
option(SPWKIT_BUILD_UDP "Build the distributed VSPW-TP/UDP backend when supported" ON)
option(SPWKIT_BUILD_TOOLS "Build SpWKit command-line tools" OFF)
option(SPWKIT_ENABLE_HEAP "Enable heap-backed spw_port_open convenience API" ON)
option(SPWKIT_ENABLE_CPP "Enable/install the optional C++17 convenience wrapper" OFF)

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

# Enable CXX only when an explicitly requested build product needs it. This is
# deliberately independent from SPWKIT_BUILD_TESTS/SPWKIT_BUILD_EXAMPLES so a
# pure-C project can build and execute the C test/example surface with no C++
# compiler installed.
set(SPWKIT_NEEDS_CXX OFF)
if(SPWKIT_ENABLE_CPP)
    set(SPWKIT_NEEDS_CXX ON)
endif()
if(SPWKIT_BUILD_TESTS AND SPWKIT_BUILD_CPP_TESTS)
    set(SPWKIT_NEEDS_CXX ON)
endif()
if(SPWKIT_BUILD_EXAMPLES AND SPWKIT_BUILD_CPP_EXAMPLES)
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
    src/backends/device/vspw_device_protocol.c
    src/backends/ethernet/vspw_tp.c
    src/backends/ethernet/fragment_reassembler.c
    src/backends/ethernet/virtual_link_timing.c
    src/backends/ethernet/deterministic_faults.c
)

if(SPWKIT_BUILD_SIMULATOR)
    list(APPEND SPWKIT_SOURCES
        src/platform/host_sync.c
        src/backends/virtual/simulator_backend.c
    )
endif()

if(SPWKIT_UDP_RUNTIME_SUPPORTED)
    list(APPEND SPWKIT_SOURCES
        src/backends/ethernet/udp_backend.c
    )
endif()

# BUILD_SHARED_LIBS is intentionally the standard switch. OFF (the CMake
# default) gives the embedded/static profile; hosted builds may opt into a
# shared library without changing the public target or API.
add_library(spwkit ${SPWKIT_SOURCES})
add_library(spwkit::spwkit ALIAS spwkit)

target_compile_features(spwkit PRIVATE c_std_11)

target_include_directories(spwkit
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set(SPWKIT_HOST_THREADS_REQUIRED OFF)
if(SPWKIT_BUILD_SIMULATOR)
    target_compile_definitions(spwkit PRIVATE SPWKIT_HAS_SIMULATOR=1)
    if(UNIX)
        set(SPWKIT_HOST_THREADS_REQUIRED ON)
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

# Optional header-only C++17 convenience layer. It contains no backend logic;
# all operations delegate to the installed public C API in spwkit::spwkit.
if(SPWKIT_ENABLE_CPP)
    add_library(spwkit_cpp INTERFACE)
    add_library(spwkit::cpp ALIAS spwkit_cpp)
    target_link_libraries(spwkit_cpp INTERFACE spwkit::spwkit)
    target_compile_features(spwkit_cpp INTERFACE cxx_std_17)
    target_include_directories(spwkit_cpp INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    set_target_properties(spwkit_cpp PROPERTIES EXPORT_NAME cpp)
endif()

if(SPWKIT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(SPWKIT_BUILD_TESTS)
    # Keep package/API version alignment executable instead of relying on
    # manually updated literals in C/C++ probes at every development bump.
    add_compile_definitions(
        SPWKIT_TEST_PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        SPWKIT_TEST_PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        SPWKIT_TEST_PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    )
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

if(SPWKIT_ENABLE_CPP)
    install(TARGETS spwkit_cpp EXPORT SpWKitTargets)
endif()

# C headers are always installed. The optional C++ convenience header is only
# installed when explicitly requested, so a C-only package has no C++ surface.
install(
    DIRECTORY include/spwkit
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN "*.hpp" EXCLUDE
)
if(SPWKIT_ENABLE_CPP)
    install(
        FILES include/spwkit/spwkit.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spwkit
    )
endif()

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
    NAMESPACE spwkit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SpWKit
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/SpWKitConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/SpWKitConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SpWKit
)
