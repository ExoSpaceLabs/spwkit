add_library(spwkit_api_c_compile OBJECT api_c_compile.c)
target_link_libraries(spwkit_api_c_compile PRIVATE SpWKit::spwkit)
set_target_properties(spwkit_api_c_compile PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_library(spwkit_api_cpp_compile OBJECT api_cpp_compile.cpp)
target_link_libraries(spwkit_api_cpp_compile PRIVATE SpWKit::spwkit)
target_compile_features(spwkit_api_cpp_compile PRIVATE cxx_std_17)

add_executable(spwkit_core_types_test core_types.c)
target_link_libraries(spwkit_core_types_test PRIVATE SpWKit::spwkit)
set_target_properties(spwkit_core_types_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(spwkit_loopback_backend_test loopback_backend.cpp)
target_link_libraries(spwkit_loopback_backend_test PRIVATE SpWKit::spwkit)
target_include_directories(spwkit_loopback_backend_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(spwkit_loopback_backend_test PRIVATE cxx_std_17)

add_executable(spwkit_public_port_test public_port.c)
target_link_libraries(spwkit_public_port_test PRIVATE SpWKit::spwkit)
set_target_properties(spwkit_public_port_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

if(SPWKIT_BUILD_SIMULATOR)
    target_compile_definitions(spwkit_public_port_test PRIVATE SPWKIT_TEST_SIMULATOR_ENABLED=1)

    find_package(Threads REQUIRED)
    add_executable(spwkit_simulator_link_test simulator_link.cpp)
    target_link_libraries(spwkit_simulator_link_test PRIVATE SpWKit::spwkit Threads::Threads)
    target_compile_features(spwkit_simulator_link_test PRIVATE cxx_std_17)
endif()

add_test(NAME api_headers_c_compile
         COMMAND ${CMAKE_COMMAND} -E echo "C API headers compiled successfully")
set_tests_properties(api_headers_c_compile PROPERTIES LABELS "unit;contract")

add_test(NAME api_headers_cpp_compile
         COMMAND ${CMAKE_COMMAND} -E echo "C++ API headers compiled successfully")
set_tests_properties(api_headers_cpp_compile PROPERTIES LABELS "unit;contract")

add_test(NAME core_public_types COMMAND spwkit_core_types_test)
set_tests_properties(core_public_types PROPERTIES LABELS "unit;contract")

add_test(NAME loopback_backend_contract COMMAND spwkit_loopback_backend_test)
set_tests_properties(loopback_backend_contract PROPERTIES LABELS "unit;contract")

add_test(NAME public_port_dispatch COMMAND spwkit_public_port_test)
set_tests_properties(public_port_dispatch PROPERTIES LABELS "unit;contract")

if(SPWKIT_BUILD_SIMULATOR)
    add_test(NAME simulator_local_peer_link COMMAND spwkit_simulator_link_test)
    set_tests_properties(simulator_local_peer_link PROPERTIES LABELS "simulator;integration;contract")
endif()
