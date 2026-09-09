#!/usr/bin/env python3
from pathlib import Path

path = Path('integrations/stm32h755_dma/firmware.c')
text = path.read_text()
old = '''#ifdef SPWKIT_STM32_PROFILE
#include "profile_impl.inc"
#endif
'''
new = '''#ifdef SPWKIT_STM32_PROFILE
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
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f'expected one profile include block, found {count}')
path.write_text(text.replace(old, new, 1))
