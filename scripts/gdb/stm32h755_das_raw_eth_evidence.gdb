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
set $core_hz=(unsigned int)g_spwkit_das_raw_evidence.core_hz
set $rows=(unsigned int)g_spwkit_das_raw_evidence.profile_row_count

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
printf "profile_rows=%u\n", $rows

set $profile_ok=1
set $expected_count=0
set $row=0
while $row < $rows
  set $payload=(unsigned int)g_spwkit_das_raw_evidence.profile[$row].payload_bytes
  set $count=(unsigned int)g_spwkit_das_raw_evidence.profile[$row].count
  printf "PROFILE_ROW,%u,%u\n", $payload, $count
  if $count == 0 || $count > 128
    set $profile_ok=0
  end
  if $row == 0
    set $expected_count=$count
  else
    if $count != $expected_count
      set $profile_ok=0
    end
  end

  set $sample=0
  while $sample < $count
    set $tx_api=(unsigned int)g_spwkit_das_raw_evidence.profile[$row].tx_api_cycles[$sample]
    set $tx_das=(unsigned int)g_spwkit_das_raw_evidence.profile[$row].tx_das_cycles[$sample]
    set $tx_calls=(unsigned int)g_spwkit_das_raw_evidence.profile[$row].tx_das_calls[$sample]
    set $rx_post=(unsigned int)g_spwkit_das_raw_evidence.profile[$row].rx_post_das_cycles[$sample]
    set $rx_das=(unsigned int)g_spwkit_das_raw_evidence.profile[$row].rx_das_cycles[$sample]
    set $rx_calls=(unsigned int)g_spwkit_das_raw_evidence.profile[$row].rx_das_calls[$sample]
    printf "PROFILE,%u,%u,%u,%u,%u,%u,%u,%u\n", $payload, $sample, $tx_api, $tx_das, $tx_calls, $rx_post, $rx_das, $rx_calls
    set $sample=$sample+1
  end
  set $row=$row+1
end

if $magic == 0x53504441 && $phase == 0x0000700d && $result == 0 && $workspace > 0 && $workspace <= 32768 && $max_packet == 4096 && $speed > 0 && $echoed > 0 && $tx == $echoed && $rx == ($echoed + 1) && $core_hz == 400000000 && $rows == 4 && $profile_ok == 1
  printf "RESULT: PASS\n"
else
  printf "RESULT: FAIL\n"
end

monitor resume
detach
quit
