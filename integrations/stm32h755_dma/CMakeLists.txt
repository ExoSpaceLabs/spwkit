cmake_minimum_required(VERSION 3.20)
project(spwkit_stm32h755_dma_integration LANGUAGES C ASM)

find_package(SpWKit 0.6 CONFIG REQUIRED)

if(NOT TARGET spwkit::spwkit)
    message(FATAL_ERROR "Installed SpWKit package does not export spwkit::spwkit")
endif()

set(STM32_CUBE_H7_DIR "" CACHE PATH
    "Path to a pinned STM32CubeH7 checkout containing Drivers/CMSIS")
option(SPWKIT_STM32_PROFILE_FIRMWARE
    "Build the STM32H755 Release/DWT profiling firmware instead of correctness evidence" OFF)
set(SPWKIT_SOURCE_DIR "" CACHE PATH
    "SpWKit source root, required only by the repo-internal profiling firmware")
set(SPWKIT_STM32_PROFILE_START "" CACHE STRING "Profiling start probe name")
set(SPWKIT_STM32_PROFILE_END "" CACHE STRING "Profiling end probe name")
set(SPWKIT_STM32_PROFILE_CASE "0" CACHE STRING "STM32 profile case id")
set(SPWKIT_STM32_PROFILE_WARMUP "16" CACHE STRING "Warmup iterations per payload")
set(SPWKIT_STM32_PROFILE_ITERATIONS "64" CACHE STRING "Measured iterations per payload")

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

function(spwkit_configure_stm32_target target)
    target_link_libraries(${target} PRIVATE spwkit::spwkit)
    target_include_directories(${target} PRIVATE
        "${STM32_CMSIS_CORE_INCLUDE}"
        "${STM32_CMSIS_DEVICE_INCLUDE}")
    target_compile_definitions(${target} PRIVATE
        CORE_CM7
        STM32H755xx)
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Werror
        -ffreestanding
        -fno-builtin)
    target_link_options(${target} PRIVATE
        -nostartfiles
        --specs=nosys.specs
        -Wl,--gc-sections
        -Wl,--unresolved-symbols=report-all
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${target}.map
        -T${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS OFF
        SUFFIX ".elf")
endfunction()

if(SPWKIT_STM32_PROFILE_FIRMWARE)
    if(NOT SPWKIT_SOURCE_DIR OR
       NOT EXISTS "${SPWKIT_SOURCE_DIR}/src/profiling/profile.h")
        message(FATAL_ERROR
            "SPWKIT_SOURCE_DIR must point at the SpWKit source tree for profiling firmware")
    endif()
    if(SPWKIT_STM32_PROFILE_START STREQUAL "" OR
       SPWKIT_STM32_PROFILE_END STREQUAL "")
        message(FATAL_ERROR
            "Profiling firmware requires SPWKIT_STM32_PROFILE_START and SPWKIT_STM32_PROFILE_END")
    endif()
    if(NOT SPWKIT_STM32_PROFILE_CASE MATCHES "^([1-9]|1[0-2])$")
        message(FATAL_ERROR "SPWKIT_STM32_PROFILE_CASE must be in the range 1..12")
    endif()
    if(NOT SPWKIT_STM32_PROFILE_WARMUP MATCHES "^[0-9]+$" OR
       NOT SPWKIT_STM32_PROFILE_ITERATIONS MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "STM32 profiling warmup/iterations must be non-negative/positive integers")
    endif()
    if(SPWKIT_STM32_PROFILE_ITERATIONS GREATER 256)
        message(FATAL_ERROR "STM32 profiling iterations must not exceed 256")
    endif()

    set(STM32_PROFILE_FIRMWARE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/firmware.c")
    if(SPWKIT_STM32_PROFILE_CASE GREATER_EQUAL 9)
        # Cases 9..12 use the same provider firmware and profiling markers, but
        # replace only the repo-internal measurement implementation. Keeping
        # the provider source identical is the point of the direct differential.
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/firmware.c" STM32_PROFILE_FIRMWARE_TEXT)
        string(REPLACE
            "#include \"profile_impl.inc\""
            "#include \"native_compare_impl.inc\""
            STM32_PROFILE_FIRMWARE_TEXT
            "${STM32_PROFILE_FIRMWARE_TEXT}")
        set(STM32_PROFILE_FIRMWARE_SOURCE
            "${CMAKE_CURRENT_BINARY_DIR}/firmware_native_compare.c")
        file(WRITE "${STM32_PROFILE_FIRMWARE_SOURCE}" "${STM32_PROFILE_FIRMWARE_TEXT}")
    endif()

    add_executable(spwkit_stm32h755_profile "${STM32_PROFILE_FIRMWARE_SOURCE}")
    spwkit_configure_stm32_target(spwkit_stm32h755_profile)
    target_include_directories(spwkit_stm32h755_profile PRIVATE
        "${SPWKIT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(spwkit_stm32h755_profile PRIVATE
        SPWKIT_STM32_PROFILE=1
        SPWKIT_ENABLE_PROFILING=1
        SPWKIT_PROFILE_START=${SPWKIT_STM32_PROFILE_START}
        SPWKIT_PROFILE_END=${SPWKIT_STM32_PROFILE_END}
        SPWKIT_STM32_PROFILE_CASE=${SPWKIT_STM32_PROFILE_CASE}
        SPWKIT_STM32_PROFILE_WARMUP=${SPWKIT_STM32_PROFILE_WARMUP}
        SPWKIT_STM32_PROFILE_ITERATIONS=${SPWKIT_STM32_PROFILE_ITERATIONS})
    # Debug information is retained only so GDB can extract the fixed result
    # structure. -g does not change the Release optimization level.
    target_compile_options(spwkit_stm32h755_profile PRIVATE -g3)
else()
    add_executable(spwkit_stm32h755_dma firmware.c)
    spwkit_configure_stm32_target(spwkit_stm32h755_dma)
endif()
