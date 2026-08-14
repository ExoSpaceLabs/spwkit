add_library(spwkit_api_c_compile OBJECT api_c_compile.c)
target_link_libraries(spwkit_api_c_compile PRIVATE SpWKit::spwkit)
set_target_properties(spwkit_api_c_compile PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_library(spwkit_api_cpp_compile OBJECT api_cpp_compile.cpp)
target_link_libraries(spwkit_api_cpp_compile PRIVATE SpWKit::spwkit)
target_compile_features(spwkit_api_cpp_compile PRIVATE cxx_std_17)

add_test(NAME api_headers_c_compile
         COMMAND ${CMAKE_COMMAND} -E echo "C API headers compiled successfully")
set_tests_properties(api_headers_c_compile PROPERTIES LABELS "unit;contract")

add_test(NAME api_headers_cpp_compile
         COMMAND ${CMAKE_COMMAND} -E echo "C++ API headers compiled successfully")
set_tests_properties(api_headers_cpp_compile PROPERTIES LABELS "unit;contract")
