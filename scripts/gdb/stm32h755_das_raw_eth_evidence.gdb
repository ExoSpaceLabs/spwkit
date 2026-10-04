set confirm off
set pagination off
set print pretty off
set mem inaccessible-by-default off

target extended-remote :3333
monitor halt

set $pc_value=$pc
set $lr_value=$lr
set $msp_value=$msp
set $cfsr=*(unsigned int*)0xE000ED28
set $hfsr=*(unsigned int*)0xE000ED2C
set $mmfar=*(unsigned int*)0xE000ED34
set $bfar=*(unsigned int*)0xE000ED38
set $shcsr=*(unsigned int*)0xE000ED24

# Only interpret an exception stack frame when LR is an EXC_RETURN value.
set $is_exception=(($lr_value & 0xff000000) == 0xff000000)
set $fault_sp=0
set $stacked_r0=0
set $stacked_r1=0
set $stacked_r2=0
set $stacked_r3=0
set $stacked_r12=0
set $stacked_lr=0
set $stacked_pc=0
set $stacked_xpsr=0

if $is_exception
  set $fault_sp=$msp_value
  if ($lr_value & 4) != 0
    set $fault_sp=$psp
  end

  set $stacked_r0=*(unsigned int*)($fault_sp + 0)
  set $stacked_r1=*(unsigned int*)($fault_sp + 4)
  set $stacked_r2=*(unsigned int*)($fault_sp + 8)
  set $stacked_r3=*(unsigned int*)($fault_sp + 12)
  set $stacked_r12=*(unsigned int*)($fault_sp + 16)
  set $stacked_lr=*(unsigned int*)($fault_sp + 20)
  set $stacked_pc=*(unsigned int*)($fault_sp + 24)
  set $stacked_xpsr=*(unsigned int*)($fault_sp + 28)
end

printf "halt_pc=0x%08x\n", $pc_value
printf "halt_lr=0x%08x\n", $lr_value
printf "halt_msp=0x%08x\n", $msp_value
printf "is_exception=%u\n", $is_exception
if $is_exception
  printf "fault_sp=0x%08x\n", $fault_sp
  printf "stacked_r0=0x%08x\n", $stacked_r0
  printf "stacked_r1=0x%08x\n", $stacked_r1
  printf "stacked_r2=0x%08x\n", $stacked_r2
  printf "stacked_r3=0x%08x\n", $stacked_r3
  printf "stacked_r12=0x%08x\n", $stacked_r12
  printf "stacked_lr=0x%08x\n", $stacked_lr
  printf "stacked_pc=0x%08x\n", $stacked_pc
  printf "stacked_xpsr=0x%08x\n", $stacked_xpsr
end
printf "scb_cfsr=0x%08x\n", $cfsr
printf "scb_hfsr=0x%08x\n", $hfsr
printf "scb_mmfar=0x%08x\n", $mmfar
printf "scb_bfar=0x%08x\n", $bfar
printf "scb_shcsr=0x%08x\n", $shcsr

set $magic=(unsigned int)g_spwkit_das_raw_evidence.magic
set $phase=(unsigned int)g_spwkit_das_raw_evidence.phase
set $result=(unsigned int)g_spwkit_das_raw_evidence.result
set $workspace=(unsigned int)g_spwkit_das_raw_evidence.workspace_bytes
set $max_packet=(unsigned int)g_spwkit_das_raw_evidence.max_packet_size
set $speed=(unsigned int)g_spwkit_das_raw_evidence.link_speed_mbps
set $duplex=(unsigned int)g_spwkit_das_raw_evidence.link_duplex
set $echoed=(unsigned int)g_spwkit_das_raw_evidence.echoed_packets
set $tx=(unsigned int)g_spwkit_das_raw_evidence.tx_packets
set $rx=(unsigned int)g_spwkit_das_raw_evidence.rx_packets
set $tx_bytes=(unsigned int)g_spwkit_das_raw_evidence.tx_bytes
set $rx_bytes=(unsigned int)g_spwkit_das_raw_evidence.rx_bytes

set $core_hz=(unsigned int)g_spwkit_das_raw_evidence.core_hz

set $app_tx_count=(unsigned int)g_spwkit_das_raw_evidence.app_send_cycles.count
set $app_tx_min=(unsigned int)g_spwkit_das_raw_evidence.app_send_cycles.min_cycles
set $app_tx_max=(unsigned int)g_spwkit_das_raw_evidence.app_send_cycles.max_cycles
set $app_tx_total=(unsigned long long)g_spwkit_das_raw_evidence.app_send_cycles.total_cycles

set $app_rx_count=(unsigned int)g_spwkit_das_raw_evidence.app_receive_cycles.count
set $app_rx_min=(unsigned int)g_spwkit_das_raw_evidence.app_receive_cycles.min_cycles
set $app_rx_max=(unsigned int)g_spwkit_das_raw_evidence.app_receive_cycles.max_cycles
set $app_rx_total=(unsigned long long)g_spwkit_das_raw_evidence.app_receive_cycles.total_cycles

set $das_tx_count=(unsigned int)g_spwkit_das_raw_evidence.das_tx_cycles.count
set $das_tx_min=(unsigned int)g_spwkit_das_raw_evidence.das_tx_cycles.min_cycles
set $das_tx_max=(unsigned int)g_spwkit_das_raw_evidence.das_tx_cycles.max_cycles
set $das_tx_total=(unsigned long long)g_spwkit_das_raw_evidence.das_tx_cycles.total_cycles

set $das_rx_poll_count=(unsigned int)g_spwkit_das_raw_evidence.das_rx_poll_cycles.count
set $das_rx_poll_min=(unsigned int)g_spwkit_das_raw_evidence.das_rx_poll_cycles.min_cycles
set $das_rx_poll_max=(unsigned int)g_spwkit_das_raw_evidence.das_rx_poll_cycles.max_cycles
set $das_rx_poll_total=(unsigned long long)g_spwkit_das_raw_evidence.das_rx_poll_cycles.total_cycles

set $das_rx_success_count=(unsigned int)g_spwkit_das_raw_evidence.das_rx_success_cycles.count
set $das_rx_success_min=(unsigned int)g_spwkit_das_raw_evidence.das_rx_success_cycles.min_cycles
set $das_rx_success_max=(unsigned int)g_spwkit_das_raw_evidence.das_rx_success_cycles.max_cycles
set $das_rx_success_total=(unsigned long long)g_spwkit_das_raw_evidence.das_rx_success_cycles.total_cycles
set $das_rx_empty_polls=(unsigned int)g_spwkit_das_raw_evidence.das_rx_empty_polls
set $das_tx_successes=(unsigned int)g_spwkit_das_raw_evidence.das_tx_successes
set $das_tx_failures=(unsigned int)g_spwkit_das_raw_evidence.das_tx_failures
set $das_rx_errors=(unsigned int)g_spwkit_das_raw_evidence.das_rx_errors
set $das_last_tx_size=(unsigned int)g_spwkit_das_raw_evidence.das_last_tx_size
set $das_last_rx_size=(unsigned int)g_spwkit_das_raw_evidence.das_last_rx_size

printf "magic=0x%08x\n", $magic
printf "phase=0x%08x\n", $phase
printf "result=0x%08x\n", $result
printf "workspace_bytes=%u\n", $workspace
printf "max_packet_size=%u\n", $max_packet
printf "link_speed_mbps=%u\n", $speed
printf "link_duplex=%u\n", $duplex
printf "echoed_packets=%u\n", $echoed
printf "tx_packets=%u\n", $tx
printf "rx_packets=%u\n", $rx
printf "tx_bytes=%u\n", $tx_bytes
printf "rx_bytes=%u\n", $rx_bytes

printf "core_hz=%u\n", $core_hz
printf "app_send_count=%u\n", $app_tx_count
printf "app_send_min_cycles=%u\n", $app_tx_min
printf "app_send_max_cycles=%u\n", $app_tx_max
printf "app_send_total_cycles=%llu\n", $app_tx_total
printf "app_receive_count=%u\n", $app_rx_count
printf "app_receive_min_cycles=%u\n", $app_rx_min
printf "app_receive_max_cycles=%u\n", $app_rx_max
printf "app_receive_total_cycles=%llu\n", $app_rx_total
printf "das_tx_count=%u\n", $das_tx_count
printf "das_tx_min_cycles=%u\n", $das_tx_min
printf "das_tx_max_cycles=%u\n", $das_tx_max
printf "das_tx_total_cycles=%llu\n", $das_tx_total
printf "das_rx_poll_count=%u\n", $das_rx_poll_count
printf "das_rx_poll_min_cycles=%u\n", $das_rx_poll_min
printf "das_rx_poll_max_cycles=%u\n", $das_rx_poll_max
printf "das_rx_poll_total_cycles=%llu\n", $das_rx_poll_total
printf "das_rx_success_count=%u\n", $das_rx_success_count
printf "das_rx_success_min_cycles=%u\n", $das_rx_success_min
printf "das_rx_success_max_cycles=%u\n", $das_rx_success_max
printf "das_rx_success_total_cycles=%llu\n", $das_rx_success_total
printf "das_rx_empty_polls=%u\n", $das_rx_empty_polls
printf "das_tx_successes=%u\n", $das_tx_successes
printf "das_tx_failures=%u\n", $das_tx_failures
printf "das_rx_errors=%u\n", $das_rx_errors
printf "das_last_tx_size=%u\n", $das_last_tx_size
printf "das_last_rx_size=%u\n", $das_last_rx_size
printf "das_last_tx_header=%02x:%02x:%02x:%02x:%02x:%02x %02x:%02x:%02x:%02x:%02x:%02x ethertype=%02x%02x\n", g_spwkit_das_raw_evidence.das_last_tx_header[0], g_spwkit_das_raw_evidence.das_last_tx_header[1], g_spwkit_das_raw_evidence.das_last_tx_header[2], g_spwkit_das_raw_evidence.das_last_tx_header[3], g_spwkit_das_raw_evidence.das_last_tx_header[4], g_spwkit_das_raw_evidence.das_last_tx_header[5], g_spwkit_das_raw_evidence.das_last_tx_header[6], g_spwkit_das_raw_evidence.das_last_tx_header[7], g_spwkit_das_raw_evidence.das_last_tx_header[8], g_spwkit_das_raw_evidence.das_last_tx_header[9], g_spwkit_das_raw_evidence.das_last_tx_header[10], g_spwkit_das_raw_evidence.das_last_tx_header[11], g_spwkit_das_raw_evidence.das_last_tx_header[12], g_spwkit_das_raw_evidence.das_last_tx_header[13]
printf "das_last_rx_header=%02x:%02x:%02x:%02x:%02x:%02x %02x:%02x:%02x:%02x:%02x:%02x ethertype=%02x%02x\n", g_spwkit_das_raw_evidence.das_last_rx_header[0], g_spwkit_das_raw_evidence.das_last_rx_header[1], g_spwkit_das_raw_evidence.das_last_rx_header[2], g_spwkit_das_raw_evidence.das_last_rx_header[3], g_spwkit_das_raw_evidence.das_last_rx_header[4], g_spwkit_das_raw_evidence.das_last_rx_header[5], g_spwkit_das_raw_evidence.das_last_rx_header[6], g_spwkit_das_raw_evidence.das_last_rx_header[7], g_spwkit_das_raw_evidence.das_last_rx_header[8], g_spwkit_das_raw_evidence.das_last_rx_header[9], g_spwkit_das_raw_evidence.das_last_rx_header[10], g_spwkit_das_raw_evidence.das_last_rx_header[11], g_spwkit_das_raw_evidence.das_last_rx_header[12], g_spwkit_das_raw_evidence.das_last_rx_header[13]

if $magic == 0x53504441 && $phase == 0x0000700d && $result == 0 && $workspace > 0 && $workspace <= 32768 && $max_packet == 4096 && $speed > 0 && $echoed > 0 && $tx == $echoed && $rx == ($echoed + 1) && $core_hz == 400000000 && $app_tx_count == $echoed && $app_rx_count == ($echoed + 1) && $das_tx_count > 0 && $das_rx_success_count > 0
  printf "RESULT: PASS\n"
else
  printf "RESULT: FAIL\n"
end

monitor resume
detach
quit
