# SPDX-License-Identifier: Apache-2.0

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT SPWKIT_BUILD_VSPWD)
    return()
endif()

# These integration programs use assert() as executable test assertions,
# including around operations that establish protocol state. Keep assertions
# active in Release builds just like the main tests/ directory does.
if(MSVC)
    add_compile_options(/UNDEBUG)
else()
    add_compile_options(-UNDEBUG)
endif()

add_executable(spwkit_vspwd_raw_peer ../vspwd_raw_peer.c)
target_link_libraries(spwkit_vspwd_raw_peer PRIVATE spwkit::spwkit)
target_include_directories(spwkit_vspwd_raw_peer PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(spwkit_vspwd_raw_peer PRIVATE c_std_11)
set_target_properties(spwkit_vspwd_raw_peer PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF)

add_executable(spwkit_vspwd_edge ../vspwd_edge.c)
target_link_libraries(spwkit_vspwd_edge PRIVATE spwkit::spwkit)
target_include_directories(spwkit_vspwd_edge PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(spwkit_vspwd_edge PRIVATE c_std_11)
set_target_properties(spwkit_vspwd_edge PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF)

if(SPWKIT_DEVICE_RUNTIME_SUPPORTED)
    add_executable(spwkit_device_public_peer ../device_public_peer.c)
    target_link_libraries(spwkit_device_public_peer PRIVATE spwkit::spwkit)
    target_compile_features(spwkit_device_public_peer PRIVATE c_std_11)
    set_target_properties(spwkit_device_public_peer PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS OFF)

    if(SPWKIT_UDP_RUNTIME_SUPPORTED AND SPWKIT_ENABLE_HEAP)
        add_executable(spwkit_device_bridge_peer ../device_bridge_peer.c)
        target_link_libraries(spwkit_device_bridge_peer PRIVATE spwkit::spwkit)
        target_compile_features(spwkit_device_bridge_peer PRIVATE c_std_11)
        set_target_properties(spwkit_device_bridge_peer PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED YES
            C_EXTENSIONS OFF)

        add_executable(spwkit_bridge_udp_peer ../../examples/distributed/udp_peer.c)
        target_link_libraries(spwkit_bridge_udp_peer PRIVATE spwkit::spwkit)
        target_compile_features(spwkit_bridge_udp_peer PRIVATE c_std_11)
        set_target_properties(spwkit_bridge_udp_peer PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED YES
            C_EXTENSIONS OFF)
    endif()

    if(SPWKIT_BUILD_TOOLS AND SPWKIT_ENABLE_HEAP)
        add_executable(spwkit_device_hold_peer ../device_hold_peer.c)
        target_link_libraries(spwkit_device_hold_peer PRIVATE spwkit::spwkit)
        target_compile_features(spwkit_device_hold_peer PRIVATE c_std_11)
        set_target_properties(spwkit_device_hold_peer PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED YES
            C_EXTENSIONS OFF)
    endif()

    if(SPWKIT_BUILD_CPP_TESTS AND SPWKIT_ENABLE_HEAP)
        add_executable(spwkit_device_contract_test ../contract/device_contract.cpp)
        target_link_libraries(spwkit_device_contract_test PRIVATE spwkit_backend_contract_suite)
        target_compile_features(spwkit_device_contract_test PRIVATE cxx_std_17)
    endif()
endif()

add_test(
    NAME vspwd_process_pair_restart
    COMMAND bash
            ${CMAKE_CURRENT_SOURCE_DIR}/run_vspwd_pair.sh
            $<TARGET_FILE:vspwd>
            $<TARGET_FILE:spwkit_vspwd_raw_peer>)
set_tests_properties(vspwd_process_pair_restart PROPERTIES
    LABELS "device;integration;process;restart;c"
    TIMEOUT 45)

add_test(
    NAME vspwd_process_edge_cases
    COMMAND bash
            ${CMAKE_CURRENT_SOURCE_DIR}/run_vspwd_edge.sh
            $<TARGET_FILE:vspwd>
            $<TARGET_FILE:spwkit_vspwd_edge>)
set_tests_properties(vspwd_process_edge_cases PROPERTIES
    LABELS "device;integration;process;edge;c"
    TIMEOUT 30)

if(SPWKIT_DEVICE_RUNTIME_SUPPORTED)
    add_test(
        NAME device_public_api_process_pair_restart
        COMMAND bash
                ${CMAKE_CURRENT_SOURCE_DIR}/run_vspwd_pair.sh
                $<TARGET_FILE:vspwd>
                $<TARGET_FILE:spwkit_device_public_peer>)
    set_tests_properties(device_public_api_process_pair_restart PROPERTIES
        LABELS "device;contract;integration;process;restart;c"
        TIMEOUT 45)

    if(SPWKIT_UDP_RUNTIME_SUPPORTED AND SPWKIT_BUILD_TOOLS AND SPWKIT_ENABLE_HEAP)
        add_test(
            NAME vspwd_udp_bridge_remote_restart
            COMMAND bash
                    ${CMAKE_CURRENT_SOURCE_DIR}/run_udp_bridge.sh
                    $<TARGET_FILE:vspwd>
                    $<TARGET_FILE:spwkit_device_bridge_peer>
                    $<TARGET_FILE:spwkit_bridge_udp_peer>
                    $<TARGET_FILE:spwctl>
                    $<TARGET_FILE:spwkit_example_c_device_peer>)
        set_tests_properties(vspwd_udp_bridge_remote_restart PROPERTIES
            LABELS "device;bridge;transport;integration;process;restart;c"
            TIMEOUT 60)
    endif()

    if(SPWKIT_BUILD_TOOLS AND SPWKIT_ENABLE_HEAP)
        add_test(
            NAME spwctl_management_non_owning
            COMMAND bash
                    ${CMAKE_CURRENT_SOURCE_DIR}/run_spwctl.sh
                    $<TARGET_FILE:vspwd>
                    $<TARGET_FILE:spwctl>
                    $<TARGET_FILE:spwkit_device_hold_peer>)
        set_tests_properties(spwctl_management_non_owning PROPERTIES
            LABELS "device;tools;management;integration;process;c"
            TIMEOUT 45)

        add_test(
            NAME spwmon_passive_push_observation
            COMMAND bash
                    ${CMAKE_CURRENT_SOURCE_DIR}/run_spwmon.sh
                    $<TARGET_FILE:vspwd>
                    $<TARGET_FILE:spwmon>
                    $<TARGET_FILE:spwkit_device_hold_peer>)
        set_tests_properties(spwmon_passive_push_observation PROPERTIES
            LABELS "device;tools;management;monitor;integration;process;c"
            TIMEOUT 45)
    endif()

    if(SPWKIT_BUILD_CPP_TESTS AND SPWKIT_ENABLE_HEAP)
        add_test(
            NAME backend_contract_device
            COMMAND bash
                    ${CMAKE_CURRENT_SOURCE_DIR}/run_device_contract.sh
                    $<TARGET_FILE:vspwd>
                    $<TARGET_FILE:spwkit_device_contract_test>)
        set_tests_properties(backend_contract_device PROPERTIES
            LABELS "device;contract;integration;process;restart;cpp"
            TIMEOUT 60)
    endif()
endif()
