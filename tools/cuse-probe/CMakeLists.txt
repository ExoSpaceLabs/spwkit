cmake_minimum_required(VERSION 3.20)
project(spwkit_cuse_probe LANGUAGES C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED IMPORTED_TARGET fuse3)

add_executable(spwcuse_probe
    ../../src/cuse/spwcuse_probe.c)
target_link_libraries(spwcuse_probe PRIVATE PkgConfig::FUSE3)
target_compile_features(spwcuse_probe PRIVATE c_std_11)
set_target_properties(spwcuse_probe PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF)
