set confirm off
set pagination off
set print pretty off
set mem inaccessible-by-default off

target extended-remote :3333
monitor arm semihosting disable
monitor reset halt

printf "Flashing STM32H755 CM7 profiling image...\n"
load
compare-sections

printf "Running SpWKit STM32H755 DWT profiling firmware...\n"
monitor reset run
shell sleep 2
monitor halt

set $magic=(unsigned int)g_stm32h755_spwkit_profile.magic
set $version=(unsigned int)g_stm32h755_spwkit_profile.version
set $phase=(unsigned int)g_stm32h755_spwkit_profile.phase
set $result=(unsigned int)g_stm32h755_spwkit_profile.result
set $case=(unsigned int)g_stm32h755_spwkit_profile.case_id
set $start=(unsigned int)g_stm32h755_spwkit_profile.start_probe_id
set $end=(unsigned int)g_stm32h755_spwkit_profile.end_probe_id
set $hz=(unsigned int)g_stm32h755_spwkit_profile.core_hz
set $warmup=(unsigned int)g_stm32h755_spwkit_profile.warmup_iterations
set $iterations=(unsigned int)g_stm32h755_spwkit_profile.measured_iterations
set $rows=(unsigned int)g_stm32h755_spwkit_profile.row_count
set $cache=(unsigned int)g_stm32h755_spwkit_profile.cache_valid

printf "PROFILE_META magic=0x%08x version=%u phase=0x%08x result=0x%08x case_id=%u start_id=%u end_id=%u core_hz=%u warmup=%u iterations=%u rows=%u cache_valid=%u\n", $magic, $version, $phase, $result, $case, $start, $end, $hz, $warmup, $iterations, $rows, $cache
printf "PROFILE_DEBUG sequence=%u delta=%u tx_owner=%u rx_owner=%u transfer_length=%u dma_transfers=%u tx_packets=%u rx_packets=%u\n", (unsigned int)spw_profile_state.sequence, (unsigned int)spw_profile_state.delta, (unsigned int)g_driver.tx_owner, (unsigned int)g_driver.rx_owner, (unsigned int)g_driver.transfer_length, (unsigned int)g_driver.dma_transfers, (unsigned int)g_driver.statistics.tx_packets, (unsigned int)g_driver.statistics.rx_packets

if $magic == 0x53575050 && $version == 1 && $phase == 0x0000700d && $result == 0 && $iterations > 0 && $rows == 5
  printf "RESULT: PASS\n"
else
  printf "RESULT: FAIL\n"
end

# One bulk remote-memory transfer replaces ~1000 individual GDB reads for the
# default copied-TX case. The campaign runs GDB from the case build directory.
dump binary memory stm32h755-profile.raw &g_stm32h755_spwkit_profile ((char *)&g_stm32h755_spwkit_profile)+sizeof(g_stm32h755_spwkit_profile)
printf "PROFILE_RAW bytes=%u\n", (unsigned int)sizeof(g_stm32h755_spwkit_profile)

monitor resume
detach
quit
