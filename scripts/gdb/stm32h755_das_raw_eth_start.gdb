set confirm off
set pagination off
set mem inaccessible-by-default off

target extended-remote :3333
monitor arm semihosting disable
monitor reset halt
load
compare-sections
monitor reset run
detach
quit
