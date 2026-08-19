cmake_minimum_required(VERSION 3.20)
project(spwkit_hardrt_cortex_m_integration LANGUAGES C ASM)

find_package(HardRT 0.4 CONFIG REQUIRED)
find_package(SpWKit 0.5 CONFIG REQUIRED)

if(NOT TARGET HardRT::hardrt)
    message(FATAL_ERROR "Installed HardRT package does not export HardRT::hardrt")
endif()
if(NOT TARGET spwkit::spwkit)
    message(FATAL_ERROR "Installed SpWKit package does not export spwkit::spwkit")
endif()

add_executable(spwkit_hardrt_cortex_m7 firmware.c)
target_link_libraries(spwkit_hardrt_cortex_m7 PRIVATE HardRT::hardrt spwkit::spwkit)
target_compile_options(spwkit_hardrt_cortex_m7 PRIVATE
    -Wall
    -Wextra
    -Werror
    -ffreestanding
    -fno-builtin)
target_link_options(spwkit_hardrt_cortex_m7 PRIVATE
    -nostartfiles
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--unresolved-symbols=report-all
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/spwkit_hardrt_cortex_m7.map
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
set_target_properties(spwkit_hardrt_cortex_m7 PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF
    SUFFIX ".elf")
