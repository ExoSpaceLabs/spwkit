# SPDX-License-Identifier: Apache-2.0

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT SPWKIT_BUILD_VSPWD)
    return()
endif()

add_executable(spwkit_vspwd_raw_peer ../vspwd_raw_peer.c)
target_link_libraries(spwkit_vspwd_raw_peer PRIVATE spwkit::spwkit)
target_include_directories(spwkit_vspwd_raw_peer PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(spwkit_vspwd_raw_peer PRIVATE c_std_11)
set_target_properties(spwkit_vspwd_raw_peer PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF)

add_test(
    NAME vspwd_process_pair_restart
    COMMAND bash
            ${CMAKE_CURRENT_SOURCE_DIR}/run_vspwd_pair.sh
            $<TARGET_FILE:vspwd>
            $<TARGET_FILE:spwkit_vspwd_raw_peer>)
set_tests_properties(vspwd_process_pair_restart PROPERTIES
    LABELS "device;integration;process;c"
    TIMEOUT 45)
