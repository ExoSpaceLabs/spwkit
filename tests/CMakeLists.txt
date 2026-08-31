# Test sources use assert() as executable test assertions, including around calls
# whose side effects establish test state. Release builds define NDEBUG by
# default, so explicitly undefine it for every target created in this directory.
# The library itself keeps the normal build configuration; this only hardens
# the test harness.
if(MSVC)
    add_compile_options(/UNDEBUG)
else()
    add_compile_options(-UNDEBUG)
endif()

# Windows exercises the same UDP fixtures through the private compatibility
# headers used by the runtime. Nothing from this directory is installed or
# enters the public SpWKit include surface.
if(WIN32 AND SPWKIT_UDP_RUNTIME_SUPPORTED)
    include_directories(BEFORE ${PROJECT_SOURCE_DIR}/src/platform/win32/compat)
endif()

# -----------------------------------------------------------------------------
# Pure-C public/runtime tests. These targets must stay buildable when CXX is not
# enabled, and form the behavioral core of the C-only CI profile.
# -----------------------------------------------------------------------------
add_library(spwkit_api_c_compile OBJECT api_c_compile.c)
target_link_libraries(spwkit_api_c_compile PRIVATE spwkit::spwkit)
set_target_properties(spwkit_api_c_compile PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(spwkit_core_types_test core_types.c)
target_link_libraries(spwkit_core_types_test PRIVATE spwkit::spwkit)
set_target_properties(spwkit_core_types_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(spwkit_loopback_backend_test loopback_backend.c)
target_link_libraries(spwkit_loopback_backend_test PRIVATE spwkit::spwkit)
set_target_properties(spwkit_loopback_backend_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(spwkit_public_port_test public_port.c)
target_link_libraries(spwkit_public_port_test PRIVATE spwkit::spwkit)
set_target_properties(spwkit_public_port_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(spwkit_public_edge_cases_test public_edge_cases.c)
target_link_libraries(spwkit_public_edge_cases_test PRIVATE spwkit::spwkit)
set_target_properties(spwkit_public_edge_cases_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(spwkit_vspd_protocol_test vspw_device_protocol.c)
target_link_libraries(spwkit_vspd_protocol_test PRIVATE spwkit::spwkit)
target_include_directories(spwkit_vspd_protocol_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
set_target_properties(spwkit_vspd_protocol_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(spwkit_vspd_seqpacket_test vspw_seqpacket.c)
    target_link_libraries(spwkit_vspd_seqpacket_test PRIVATE spwkit::spwkit)
    target_include_directories(spwkit_vspd_seqpacket_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    set_target_properties(spwkit_vspd_seqpacket_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
endif()

if(NOT SPWKIT_ENABLE_HEAP)
    add_executable(spwkit_no_heap_c_test no_heap_c.c)
    target_link_libraries(spwkit_no_heap_c_test PRIVATE spwkit::spwkit)
    set_target_properties(spwkit_no_heap_c_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
endif()

if(SPWKIT_BUILD_SIMULATOR)
    target_compile_definitions(spwkit_public_port_test PRIVATE SPWKIT_TEST_SIMULATOR_ENABLED=1)
    add_executable(spwkit_simulator_c_test simulator_c.c)
    target_link_libraries(spwkit_simulator_c_test PRIVATE spwkit::spwkit)
    set_target_properties(spwkit_simulator_c_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
endif()

# -----------------------------------------------------------------------------
# Development-side C++ fixtures and optional C++ wrapper compile coverage.
# These are deliberately separable from SPWKIT_BUILD_TESTS so a pure-C build can
# execute real tests with CXX=/bin/false.
# -----------------------------------------------------------------------------
if(SPWKIT_BUILD_CPP_TESTS)
    add_library(spwkit_api_cpp_compile OBJECT api_cpp_compile.cpp)
    if(SPWKIT_ENABLE_CPP)
        target_link_libraries(spwkit_api_cpp_compile PRIVATE spwkit::cpp)
    else()
        target_link_libraries(spwkit_api_cpp_compile PRIVATE spwkit::spwkit)
    endif()
    target_compile_features(spwkit_api_cpp_compile PRIVATE cxx_std_17)

    add_executable(spwkit_vspw_tp_test vspw_tp.cpp)
    target_link_libraries(spwkit_vspw_tp_test PRIVATE spwkit::spwkit)
    target_include_directories(spwkit_vspw_tp_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_features(spwkit_vspw_tp_test PRIVATE cxx_std_17)
    spwkit_apply_no_throw_policy(spwkit_vspw_tp_test)

    add_executable(spwkit_fragment_reassembler_test fragment_reassembler.cpp)
    target_link_libraries(spwkit_fragment_reassembler_test PRIVATE spwkit::spwkit)
    target_include_directories(spwkit_fragment_reassembler_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_features(spwkit_fragment_reassembler_test PRIVATE cxx_std_17)
    spwkit_apply_no_throw_policy(spwkit_fragment_reassembler_test)

    add_executable(spwkit_virtual_link_timing_test virtual_link_timing.cpp)
    target_link_libraries(spwkit_virtual_link_timing_test PRIVATE spwkit::spwkit)
    target_include_directories(spwkit_virtual_link_timing_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_features(spwkit_virtual_link_timing_test PRIVATE cxx_std_17)
    spwkit_apply_no_throw_policy(spwkit_virtual_link_timing_test)

    add_executable(spwkit_deterministic_faults_test deterministic_faults.cpp)
    target_link_libraries(spwkit_deterministic_faults_test PRIVATE spwkit::spwkit)
    target_include_directories(spwkit_deterministic_faults_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_features(spwkit_deterministic_faults_test PRIVATE cxx_std_17)
    spwkit_apply_no_throw_policy(spwkit_deterministic_faults_test)

    add_library(spwkit_backend_contract_suite STATIC
        contract/contract_suite.cpp
        contract/distributed_contract.cpp)
    target_link_libraries(spwkit_backend_contract_suite PUBLIC spwkit::spwkit)
    target_include_directories(spwkit_backend_contract_suite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/contract)
    target_compile_features(spwkit_backend_contract_suite PUBLIC cxx_std_17)

    add_executable(spwkit_loopback_contract_test contract/loopback_contract.cpp)
    target_link_libraries(spwkit_loopback_contract_test PRIVATE spwkit_backend_contract_suite)
    target_compile_features(spwkit_loopback_contract_test PRIVATE cxx_std_17)

    if(NOT SPWKIT_ENABLE_HEAP)
        add_executable(spwkit_no_heap_cpp_test no_heap.cpp)
        target_link_libraries(spwkit_no_heap_cpp_test PRIVATE spwkit::spwkit)
        target_compile_features(spwkit_no_heap_cpp_test PRIVATE cxx_std_17)
    endif()

    if(SPWKIT_BUILD_SIMULATOR)
        find_package(Threads REQUIRED)
        add_executable(spwkit_simulator_link_test simulator_link.cpp)
        target_link_libraries(spwkit_simulator_link_test PRIVATE spwkit::spwkit Threads::Threads)
        target_compile_features(spwkit_simulator_link_test PRIVATE cxx_std_17)

        add_executable(spwkit_simulator_edge_cases_test simulator_edge_cases.cpp)
        target_link_libraries(spwkit_simulator_edge_cases_test PRIVATE spwkit::spwkit)
        target_compile_features(spwkit_simulator_edge_cases_test PRIVATE cxx_std_17)

        add_executable(spwkit_simulator_contract_test contract/simulator_contract.cpp)
        target_link_libraries(spwkit_simulator_contract_test PRIVATE spwkit_backend_contract_suite)
        target_compile_features(spwkit_simulator_contract_test PRIVATE cxx_std_17)
    endif()

    if(SPWKIT_UDP_RUNTIME_SUPPORTED AND SPWKIT_ENABLE_HEAP)
        add_executable(spwkit_udp_backend_test udp_backend.cpp)
        target_link_libraries(spwkit_udp_backend_test PRIVATE spwkit::spwkit)
        target_compile_features(spwkit_udp_backend_test PRIVATE cxx_std_17)

        add_executable(spwkit_udp_reordering_test udp_reordering.cpp)
        target_link_libraries(spwkit_udp_reordering_test PRIVATE spwkit::spwkit)
        target_include_directories(spwkit_udp_reordering_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
        target_compile_features(spwkit_udp_reordering_test PRIVATE cxx_std_17)
        spwkit_apply_no_throw_policy(spwkit_udp_reordering_test)

        add_executable(spwkit_udp_timing_test udp_timing.cpp)
        target_link_libraries(spwkit_udp_timing_test PRIVATE spwkit::spwkit)
        target_compile_features(spwkit_udp_timing_test PRIVATE cxx_std_17)

        add_executable(spwkit_udp_faults_test udp_faults.cpp)
        target_link_libraries(spwkit_udp_faults_test PRIVATE spwkit::spwkit)
        target_compile_features(spwkit_udp_faults_test PRIVATE cxx_std_17)

        add_executable(spwkit_udp_contract_test contract/udp_contract.cpp)
        target_link_libraries(spwkit_udp_contract_test PRIVATE spwkit_backend_contract_suite)
        target_compile_features(spwkit_udp_contract_test PRIVATE cxx_std_17)
    endif()
endif()

add_test(NAME api_headers_c_compile COMMAND ${CMAKE_COMMAND} -E echo "C API headers compiled successfully")
set_tests_properties(api_headers_c_compile PROPERTIES LABELS "unit;contract;c")
add_test(NAME core_public_types COMMAND spwkit_core_types_test)
set_tests_properties(core_public_types PROPERTIES LABELS "unit;contract;c")
add_test(NAME loopback_backend_unit COMMAND spwkit_loopback_backend_test)
set_tests_properties(loopback_backend_unit PROPERTIES LABELS "unit;c")
add_test(NAME public_port_dispatch COMMAND spwkit_public_port_test)
set_tests_properties(public_port_dispatch PROPERTIES LABELS "unit;contract;c")
add_test(NAME public_api_edge_cases COMMAND spwkit_public_edge_cases_test)
set_tests_properties(public_api_edge_cases PROPERTIES LABELS "unit;contract;edge;c")
add_test(NAME vspd_protocol_codec COMMAND spwkit_vspd_protocol_test)
set_tests_properties(vspd_protocol_codec PROPERTIES LABELS "unit;device;protocol;edge;c")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME vspd_seqpacket_transport COMMAND spwkit_vspd_seqpacket_test)
    set_tests_properties(vspd_seqpacket_transport PROPERTIES LABELS "device;protocol;integration;c")
endif()

if(NOT SPWKIT_ENABLE_HEAP)
    add_test(NAME core_no_heap_c_path COMMAND spwkit_no_heap_c_test)
    set_tests_properties(core_no_heap_c_path PROPERTIES LABELS "noheap;contract;c")
endif()

if(SPWKIT_BUILD_SIMULATOR)
    add_test(NAME simulator_c_peer_link COMMAND spwkit_simulator_c_test)
    set_tests_properties(simulator_c_peer_link PROPERTIES LABELS "simulator;integration;contract;c")
endif()

if(SPWKIT_BUILD_CPP_TESTS)
    add_test(NAME api_headers_cpp_compile COMMAND ${CMAKE_COMMAND} -E echo "C++ API headers compiled successfully")
    set_tests_properties(api_headers_cpp_compile PROPERTIES LABELS "unit;contract;cpp")
    add_test(NAME vspw_tp_codec COMMAND spwkit_vspw_tp_test)
    set_tests_properties(vspw_tp_codec PROPERTIES LABELS "unit;transport;cpp")
    add_test(NAME fragment_reassembler COMMAND spwkit_fragment_reassembler_test)
    set_tests_properties(fragment_reassembler PROPERTIES LABELS "unit;transport;edge;cpp")
    add_test(NAME virtual_link_timing COMMAND spwkit_virtual_link_timing_test)
    set_tests_properties(virtual_link_timing PROPERTIES LABELS "unit;transport;timing;cpp")
    add_test(NAME deterministic_fault_rules COMMAND spwkit_deterministic_faults_test)
    set_tests_properties(deterministic_fault_rules PROPERTIES LABELS "unit;transport;fault;cpp")
    add_test(NAME backend_contract_loopback COMMAND spwkit_loopback_contract_test)
    set_tests_properties(backend_contract_loopback PROPERTIES LABELS "contract;cpp")

    if(NOT SPWKIT_ENABLE_HEAP)
        add_test(NAME core_no_heap_cpp_path COMMAND spwkit_no_heap_cpp_test)
        set_tests_properties(core_no_heap_cpp_path PROPERTIES LABELS "noheap;contract;cpp")
    endif()

    if(SPWKIT_BUILD_SIMULATOR)
        add_test(NAME simulator_local_peer_link COMMAND spwkit_simulator_link_test)
        set_tests_properties(simulator_local_peer_link PROPERTIES LABELS "simulator;integration;cpp")
        add_test(NAME simulator_edge_cases COMMAND spwkit_simulator_edge_cases_test)
        set_tests_properties(simulator_edge_cases PROPERTIES LABELS "simulator;integration;edge;contract;cpp")
        add_test(NAME backend_contract_simulator COMMAND spwkit_simulator_contract_test)
        set_tests_properties(backend_contract_simulator PROPERTIES LABELS "simulator;integration;contract;cpp")
    endif()

    if(SPWKIT_UDP_RUNTIME_SUPPORTED AND SPWKIT_ENABLE_HEAP)
        add_test(NAME udp_distributed_backend COMMAND spwkit_udp_backend_test)
        set_tests_properties(udp_distributed_backend PROPERTIES LABELS "transport;integration;d2d;cpp")
        add_test(NAME udp_out_of_order_reassembly COMMAND spwkit_udp_reordering_test)
        set_tests_properties(udp_out_of_order_reassembly PROPERTIES LABELS "transport;integration;d2d;edge;cpp")
        add_test(NAME udp_virtual_link_timing COMMAND spwkit_udp_timing_test)
        set_tests_properties(udp_virtual_link_timing PROPERTIES LABELS "transport;integration;d2d;timing;cpp")
        add_test(NAME udp_deterministic_faults COMMAND spwkit_udp_faults_test)
        set_tests_properties(udp_deterministic_faults PROPERTIES LABELS "transport;integration;d2d;fault;edge;cpp")
        add_test(NAME backend_contract_udp COMMAND spwkit_udp_contract_test)
        set_tests_properties(backend_contract_udp PROPERTIES LABELS "contract;transport;integration;d2d;cpp")
    endif()
endif()

if(SPWKIT_BUILD_EXAMPLES)
    add_test(NAME example_c_loopback COMMAND spwkit_example_c_loopback)
    set_tests_properties(example_c_loopback PROPERTIES LABELS "example;contract;c")
    if(SPWKIT_BUILD_SIMULATOR)
        add_test(NAME example_c_simulator_zero_copy COMMAND spwkit_example_c_simulator_zero_copy)
        set_tests_properties(example_c_simulator_zero_copy PROPERTIES LABELS "example;simulator;contract;c")
    endif()
    if(SPWKIT_BUILD_CPP_EXAMPLES)
        add_test(NAME example_cpp_no_heap COMMAND spwkit_example_cpp_no_heap)
        set_tests_properties(example_cpp_no_heap PROPERTIES LABELS "example;contract;noheap;cpp")
        if(SPWKIT_ENABLE_CPP)
            add_test(NAME example_cpp_wrapper_loopback COMMAND spwkit_example_cpp_wrapper_loopback)
            set_tests_properties(example_cpp_wrapper_loopback PROPERTIES LABELS "example;contract;cpp;wrapper")
        endif()
    endif()
endif()

# Portable hardware-driver adapter contract.
add_executable(spwkit_driver_backend driver_backend.c)
target_link_libraries(spwkit_driver_backend PRIVATE spwkit::spwkit)
target_compile_features(spwkit_driver_backend PRIVATE c_std_11)
set_target_properties(spwkit_driver_backend PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS OFF)
add_test(NAME driver_backend COMMAND spwkit_driver_backend)

add_executable(spwkit_driver_dma driver_dma.c)
target_link_libraries(spwkit_driver_dma PRIVATE spwkit::spwkit)
target_compile_features(spwkit_driver_dma PRIVATE c_std_11)
set_target_properties(spwkit_driver_dma PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS OFF)
add_test(NAME driver_dma COMMAND spwkit_driver_dma)
