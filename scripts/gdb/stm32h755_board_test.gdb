set confirm off
set pagination off
set print pretty off
set mem inaccessible-by-default off

target extended-remote :3333
monitor arm semihosting disable
monitor reset halt

printf "Flashing STM32H755 CM7 image...\n"
load
compare-sections

printf "Running SpWKit STM32H755 DMA/cache contract...\n"
monitor reset run
shell sleep 2
monitor halt

set $magic=(unsigned int)g_stm32h755_spwkit_evidence.magic
set $phase=(unsigned int)g_stm32h755_spwkit_evidence.phase
set $result=(unsigned int)g_stm32h755_spwkit_evidence.result
set $sync_to=(unsigned int)g_stm32h755_spwkit_evidence.sync_to_device
set $sync_from=(unsigned int)g_stm32h755_spwkit_evidence.sync_from_device
set $dma=(unsigned int)g_stm32h755_spwkit_evidence.dma_transfers
set $tx=(unsigned int)g_stm32h755_spwkit_evidence.tx_packets
set $rx=(unsigned int)g_stm32h755_spwkit_evidence.rx_packets
set $reset_stale=(unsigned int)g_stm32h755_spwkit_evidence.reset_stale_invalidated

printf "magic=0x%08x\n", $magic
printf "phase=0x%08x\n", $phase
printf "result=0x%08x\n", $result
printf "sync_to_device=%u\n", $sync_to
printf "sync_from_device=%u\n", $sync_from
printf "dma_transfers=%u\n", $dma
printf "tx_packets=%u\n", $tx
printf "rx_packets=%u\n", $rx
printf "reset_stale_invalidated=%u\n", $reset_stale

if $magic == 0x53505736 && $phase == 0x0000700d && $result == 0 && $sync_to > 0 && $sync_from > 0 && $dma >= 2 && $tx >= 2 && $rx >= 2 && $reset_stale == 1
  printf "RESULT: PASS\n"
else
  printf "RESULT: FAIL\n"
end

monitor resume
detach
quit
