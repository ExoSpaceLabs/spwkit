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

set $i=0
while $i < $iterations
  printf "PROFILE_FLOOR index=%u cycles=%u\n", $i, (unsigned int)g_stm32h755_spwkit_profile.floor_samples[$i]
  set $i=$i+1
end

set $r=0
while $r < $rows
  set $payload=(unsigned int)g_stm32h755_spwkit_profile.rows[$r].payload_bytes
  set $i=0
  while $i < $iterations
    printf "PROFILE_SAMPLE row=%u payload=%u index=%u cycles=%u\n", $r, $payload, $i, (unsigned int)g_stm32h755_spwkit_profile.rows[$r].samples[$i]
    set $i=$i+1
  end
  set $r=$r+1
end

if $cache == 1
  set $r=0
  while $r < $rows
    set $payload=(unsigned int)g_stm32h755_spwkit_profile.cache_clean_rows[$r].payload_bytes
    set $i=0
    while $i < $iterations
      printf "PROFILE_CACHE_CLEAN row=%u payload=%u index=%u cycles=%u\n", $r, $payload, $i, (unsigned int)g_stm32h755_spwkit_profile.cache_clean_rows[$r].samples[$i]
      printf "PROFILE_CACHE_INVALIDATE row=%u payload=%u index=%u cycles=%u\n", $r, $payload, $i, (unsigned int)g_stm32h755_spwkit_profile.cache_invalidate_rows[$r].samples[$i]
      set $i=$i+1
    end
    set $r=$r+1
  end
end

if $magic == 0x53575050 && $version == 1 && $phase == 0x0000700d && $result == 0 && $iterations > 0 && $rows == 5
  printf "RESULT: PASS\n"
else
  printf "RESULT: FAIL\n"
end

monitor resume
detach
quit
