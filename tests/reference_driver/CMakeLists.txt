# Deterministic fixed-storage implementation of the public hardware-driver ABI.
add_library(spwkit_reference_driver STATIC reference_driver.c)
target_link_libraries(spwkit_reference_driver PUBLIC spwkit::spwkit)
target_include_directories(spwkit_reference_driver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(spwkit_reference_driver PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF)

if(SPWKIT_BUILD_CPP_TESTS)
    add_executable(spwkit_driver_contract_test driver_contract.cpp)
    target_link_libraries(spwkit_driver_contract_test PRIVATE
        spwkit_backend_contract_suite
        spwkit_reference_driver)
    target_compile_features(spwkit_driver_contract_test PRIVATE cxx_std_17)

    add_test(NAME backend_contract_driver COMMAND spwkit_driver_contract_test)
    set_tests_properties(backend_contract_driver PROPERTIES
        LABELS "contract;driver;hardware-boundary;zero-copy;cpp")
endif()
