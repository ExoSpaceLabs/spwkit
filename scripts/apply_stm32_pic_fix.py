#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    path.write_text(text.replace(old, new, 1))

root = Path('CMakeLists.txt')
replace_once(
    root,
    '''set_target_properties(spwkit PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
''',
    '''if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    # Bare-metal static images have no dynamic loader and should not depend on
    # a runtime GOT. Hosted builds retain the existing PIC behavior.
    set(SPWKIT_TARGET_PIC OFF)
else()
    set(SPWKIT_TARGET_PIC ON)
endif()

set_target_properties(spwkit PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ${SPWKIT_TARGET_PIC}
    VERSION ${PROJECT_VERSION}
''',
    'generic PIC policy',
)

linker = Path('integrations/stm32h755_dma/linker.ld')
replace_once(
    linker,
    '''        __data_start__ = .;
        *(.data*)
        __data_end__ = .;
''',
    '''        __data_start__ = .;
        *(.data*)
        /* Defensive startup support: if a future dependency emits GOT data,
         * initialize it with the ordinary data image instead of relying on
         * debugger-preserved SRAM contents across reset. */
        *(.got*)
        __data_end__ = .;
''',
    'GOT startup initialization',
)

firmware = Path('integrations/stm32h755_dma/firmware.c')
replace_once(
    firmware,
    '''#ifdef SPWKIT_STM32_PROFILE
/* The physical profiling firmware reads the exported profiling state directly.
 * This keeps capture on the exact object written by cross-TU probe macros and
 * avoids an extra static-library indirection in the freestanding image. */
static const volatile spw_profile_sample_t* stm32_profile_last_sample_direct(void) {
    return &spw_profile_state;
}
#define spw_profile_last_sample stm32_profile_last_sample_direct
#include "profile_impl.inc"
#undef spw_profile_last_sample
#endif
''',
    '''#ifdef SPWKIT_STM32_PROFILE
#include "profile_impl.inc"
#endif
''',
    'remove direct-state workaround',
)

workflow = Path('.github/workflows/stm32h755-dma.yml')
replace_once(
    workflow,
    '''          cmake --build build/stm32-profile-spwkit --parallel 2
          cmake --install build/stm32-profile-spwkit

          cmake -S integrations/stm32h755_dma -B build/stm32h755-profile \\
''',
    '''          cmake --build build/stm32-profile-spwkit --parallel 2

          port_obj="build/stm32-profile-spwkit/CMakeFiles/spwkit.dir/src/core/port.c.obj"
          test -s "${port_obj}"
          if arm-none-eabi-readelf -r "${port_obj}" | grep -q 'GOT'; then
            echo 'Bare-metal Cortex-M7 SpWKit unexpectedly contains GOT relocations' >&2
            arm-none-eabi-readelf -r "${port_obj}" >&2
            exit 1
          fi

          cmake --install build/stm32-profile-spwkit

          cmake -S integrations/stm32h755_dma -B build/stm32h755-profile \\
''',
    'non-PIC CI guard',
)

# Validate that the final profile image does not expose a separate uninitialized
# GOT output section. Any .got input is folded into initialized .data by linker.ld.
replace_once(
    workflow,
    '''          elf="build/stm32h755-profile/spwkit_stm32h755_profile.elf"
          test -s "${elf}"
          arm-none-eabi-nm -g "${elf}" | grep -q 'g_stm32h755_spwkit_profile'
''',
    '''          elf="build/stm32h755-profile/spwkit_stm32h755_profile.elf"
          test -s "${elf}"
          arm-none-eabi-nm -g "${elf}" | grep -q 'g_stm32h755_spwkit_profile'
          if arm-none-eabi-readelf -SW "${elf}" | grep -Eq '[[:space:]]\\.got(\\.plt)?[[:space:]]'; then
            echo 'STM32H755 profile image exposes a standalone GOT section' >&2
            arm-none-eabi-readelf -SW "${elf}" >&2
            exit 1
          fi
''',
    'final ELF GOT guard',
)
