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
