#!/usr/bin/env bash
set -Eeuo pipefail

ROLE="${ROLE:?ROLE must select a UDP or RAW Ethernet endpoint mode}"
PAYLOAD_SIZE="${PAYLOAD_SIZE:-4096}"
TOTAL_BYTES="${TOTAL_BYTES:-1073741824}"

case "$ROLE" in
  uni-source)
    exec spwkit_udp_perf --role source       --local-address 172.30.0.10 --remote-address 172.30.0.11       --local-port 43000 --remote-port 43001 --link-id 264       --payload-size "$PAYLOAD_SIZE" --total-bytes "$TOTAL_BYTES"
    ;;
  uni-sink)
    exec spwkit_udp_perf --role sink       --local-address 172.30.0.11 --remote-address 172.30.0.10       --local-port 43001 --remote-port 43000 --link-id 264       --payload-size "$PAYLOAD_SIZE" --total-bytes "$TOTAL_BYTES"
    ;;
  duplex-a)
    spwkit_udp_perf --role sink       --local-address 172.30.0.10 --remote-address 172.30.0.11       --local-port 44000 --remote-port 44001 --link-id 265       --payload-size "$PAYLOAD_SIZE" --total-bytes "$TOTAL_BYTES" &
    sink_pid=$!
    spwkit_udp_perf --role source       --local-address 172.30.0.10 --remote-address 172.30.0.11       --local-port 43000 --remote-port 43001 --link-id 264       --payload-size "$PAYLOAD_SIZE" --total-bytes "$TOTAL_BYTES"
    source_rc=$?
    wait "$sink_pid"
    sink_rc=$?
    (( source_rc == 0 && sink_rc == 0 ))
    ;;
  duplex-b)
    spwkit_udp_perf --role sink       --local-address 172.30.0.11 --remote-address 172.30.0.10       --local-port 43001 --remote-port 43000 --link-id 264       --payload-size "$PAYLOAD_SIZE" --total-bytes "$TOTAL_BYTES" &
    sink_pid=$!
    spwkit_udp_perf --role source       --local-address 172.30.0.11 --remote-address 172.30.0.10       --local-port 44001 --remote-port 44000 --link-id 265       --payload-size "$PAYLOAD_SIZE" --total-bytes "$TOTAL_BYTES"
    source_rc=$?
    wait "$sink_pid"
    sink_rc=$?
    (( source_rc == 0 && sink_rc == 0 ))
    ;;
  raw-uni-source)
    exec spwkit_raw_ethernet_perf --role source \
      --interface eth0 --remote-mac 02:42:ac:1e:00:11 \
      --link-id 364 --payload-size "$PAYLOAD_SIZE" --total-bytes "$TOTAL_BYTES"
    ;;
  raw-uni-sink)
    exec spwkit_raw_ethernet_perf --role sink \
      --interface eth0 --remote-mac 02:42:ac:1e:00:10 \
      --link-id 364 --payload-size "$PAYLOAD_SIZE" --total-bytes "$TOTAL_BYTES"
    ;;
  raw-duplex-a)
    spwkit_raw_ethernet_perf --role sink \
      --interface eth0 --remote-mac 02:42:ac:1e:00:11 \
      --link-id 365 --payload-size "$PAYLOAD_SIZE" --total-bytes "$TOTAL_BYTES" &
    sink_pid=$!
    spwkit_raw_ethernet_perf --role source \
      --interface eth0 --remote-mac 02:42:ac:1e:00:11 \
      --link-id 364 --payload-size "$PAYLOAD_SIZE" --total-bytes "$TOTAL_BYTES"
    source_rc=$?
    wait "$sink_pid"
    sink_rc=$?
    (( source_rc == 0 && sink_rc == 0 ))
    ;;
  raw-duplex-b)
    spwkit_raw_ethernet_perf --role sink \
      --interface eth0 --remote-mac 02:42:ac:1e:00:10 \
      --link-id 364 --payload-size "$PAYLOAD_SIZE" --total-bytes "$TOTAL_BYTES" &
    sink_pid=$!
    spwkit_raw_ethernet_perf --role source \
      --interface eth0 --remote-mac 02:42:ac:1e:00:10 \
      --link-id 365 --payload-size "$PAYLOAD_SIZE" --total-bytes "$TOTAL_BYTES"
    source_rc=$?
    wait "$sink_pid"
    sink_rc=$?
    (( source_rc == 0 && sink_rc == 0 ))
    ;;
  *)
    echo "invalid ROLE=$ROLE" >&2
    exit 2
    ;;
esac
