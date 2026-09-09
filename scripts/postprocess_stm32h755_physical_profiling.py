#!/usr/bin/env python3
from pathlib import Path

profile = Path('integrations/stm32h755_dma/profile_impl.inc')
text = profile.read_text()
for name in (
    'profile_copied_once',
    'profile_prepare_zc_tx',
    'profile_release_rx_ready',
    'profile_reclaim_release_tx',
    'profile_zc_once',
):
    old = f'static int {name}'
    new = f'static int __attribute__((unused)) {name}'
    if old not in text:
        raise SystemExit(f'missing helper {name}')
    text = text.replace(old, new, 1)
old = '''    spw_buffer_t* tx = NULL;\n    spw_buffer_t* reclaimed = NULL;\n    spw_buffer_t* rx = NULL;\n\n#if SPWKIT_STM32_PROFILE_CASE == 3\n'''
new = '''    spw_buffer_t* tx = NULL;\n    spw_buffer_t* reclaimed = NULL;\n    spw_buffer_t* rx = NULL;\n    (void)tx;\n    (void)reclaimed;\n    (void)rx;\n\n#if SPWKIT_STM32_PROFILE_CASE == 3\n'''
if old not in text:
    raise SystemExit('profile_zc_once locals block not found')
text = text.replace(old, new, 1)
profile.write_text(text)

firmware = Path('integrations/stm32h755_dma/firmware.c')
text = firmware.read_text()
old = 'static int run_contract(void) {'
new = 'static int __attribute__((unused)) run_contract(void) {'
if old not in text:
    raise SystemExit('run_contract declaration not found')
firmware.write_text(text.replace(old, new, 1))
