add_executable(spwkit_example_c_loopback c_loopback.c)
target_link_libraries(spwkit_example_c_loopback PRIVATE SpWKit::spwkit)
set_target_properties(spwkit_example_c_loopback PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
)

add_executable(spwkit_example_cpp_no_heap cpp_no_heap.cpp)
target_link_libraries(spwkit_example_cpp_no_heap PRIVATE SpWKit::spwkit)
target_compile_features(spwkit_example_cpp_no_heap PRIVATE cxx_std_17)

if(SPWKIT_BUILD_SIMULATOR)
    add_executable(spwkit_example_c_simulator_zero_copy c_simulator_zero_copy.c)
    target_link_libraries(spwkit_example_c_simulator_zero_copy PRIVATE SpWKit::spwkit)
    set_target_properties(spwkit_example_c_simulator_zero_copy PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
    )
endif()
