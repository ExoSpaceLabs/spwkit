set confirm off
set pagination off
set print pretty off
set mem inaccessible-by-default off

target extended-remote :3333
monitor halt

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

if $magic == 0x53504441 && $phase == 0x0000700d && $result == 0 && $workspace > 0 && $workspace <= 32768 && $max_packet == 4096 && $speed > 0 && $echoed > 0 && $tx == $echoed && $rx == ($echoed + 1)
  printf "RESULT: PASS\n"
else
  printf "RESULT: FAIL\n"
end

monitor resume
detach
quit
