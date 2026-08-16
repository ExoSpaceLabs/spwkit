add_executable(spwkit_example_c_loopback c_loopback.c)
target_link_libraries(spwkit_example_c_loopback PRIVATE spwkit::spwkit)
set_target_properties(spwkit_example_c_loopback PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
)

if(SPWKIT_BUILD_SIMULATOR)
    add_executable(spwkit_example_c_simulator_zero_copy c_simulator_zero_copy.c)
    target_link_libraries(spwkit_example_c_simulator_zero_copy PRIVATE spwkit::spwkit)
    set_target_properties(spwkit_example_c_simulator_zero_copy PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
    )
endif()

if(SPWKIT_BUILD_CPP_EXAMPLES)
    add_executable(spwkit_example_cpp_no_heap cpp_no_heap.cpp)
    target_link_libraries(spwkit_example_cpp_no_heap PRIVATE spwkit::spwkit)
    target_compile_features(spwkit_example_cpp_no_heap PRIVATE cxx_std_17)
    spwkit_apply_no_throw_policy(spwkit_example_cpp_no_heap)

    if(SPWKIT_ENABLE_CPP)
        add_executable(spwkit_example_cpp_wrapper_loopback cpp_wrapper_loopback.cpp)
        target_link_libraries(spwkit_example_cpp_wrapper_loopback PRIVATE spwkit::cpp)
        target_compile_features(spwkit_example_cpp_wrapper_loopback PRIVATE cxx_std_17)
        spwkit_apply_no_throw_policy(spwkit_example_cpp_wrapper_loopback)
    endif()
endif()
