cmake_minimum_required(VERSION 3.20)
project(spwkit_stm32h755_dma_integration LANGUAGES C ASM)

find_package(SpWKit 0.6 CONFIG REQUIRED)

if(NOT TARGET spwkit::spwkit)
    message(FATAL_ERROR "Installed SpWKit package does not export spwkit::spwkit")
endif()

set(STM32_CUBE_H7_DIR "" CACHE PATH
    "Path to a pinned STM32CubeH7 checkout containing Drivers/CMSIS")

if(NOT STM32_CUBE_H7_DIR)
    message(FATAL_ERROR
        "STM32_CUBE_H7_DIR is required. Use the pinned STM32CubeH7 release documented in README.md")
endif()

set(STM32_CMSIS_CORE_INCLUDE
    "${STM32_CUBE_H7_DIR}/Drivers/CMSIS/Include")
set(STM32_CMSIS_DEVICE_INCLUDE
    "${STM32_CUBE_H7_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Include")

if(NOT EXISTS "${STM32_CMSIS_CORE_INCLUDE}/core_cm7.h")
    message(FATAL_ERROR
        "CMSIS Cortex-M7 core headers were not found under ${STM32_CMSIS_CORE_INCLUDE}")
endif()
if(NOT EXISTS "${STM32_CMSIS_DEVICE_INCLUDE}/stm32h755xx.h")
    message(FATAL_ERROR
        "STM32H755 CMSIS device headers were not found under ${STM32_CMSIS_DEVICE_INCLUDE}; initialize the STM32H7xx CMSIS device submodule")
endif()

add_executable(spwkit_stm32h755_dma firmware.c)
target_link_libraries(spwkit_stm32h755_dma PRIVATE spwkit::spwkit)
target_include_directories(spwkit_stm32h755_dma PRIVATE
    "${STM32_CMSIS_CORE_INCLUDE}"
    "${STM32_CMSIS_DEVICE_INCLUDE}")
target_compile_definitions(spwkit_stm32h755_dma PRIVATE
    CORE_CM7
    STM32H755xx)
target_compile_options(spwkit_stm32h755_dma PRIVATE
    -Wall
    -Wextra
    -Werror
    -ffreestanding
    -fno-builtin)
target_link_options(spwkit_stm32h755_dma PRIVATE
    -nostartfiles
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--unresolved-symbols=report-all
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/spwkit_stm32h755_dma.map
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
set_target_properties(spwkit_stm32h755_dma PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF
    SUFFIX ".elf")
