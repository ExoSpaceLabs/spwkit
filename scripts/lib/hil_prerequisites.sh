#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Prepare only missing Ubuntu/Debian HIL dependencies. Never remove global tools.
spwkit_hil_prepare_tools() {
  local cmd package entry
  local -a missing=()
  local -a dependencies=(
    'cmake:cmake' 'git:git' 'openocd:openocd'
    'arm-none-eabi-gcc:gcc-arm-none-eabi'
    'arm-none-eabi-nm:binutils-arm-none-eabi'
    'arm-none-eabi-size:binutils-arm-none-eabi'
    'arm-none-eabi-addr2line:binutils-arm-none-eabi'
    'ip:iproute2' 'python3:python3' 'timeout:coreutils'
    'tee:coreutils' 'grep:grep'
  )
  command -v sudo >/dev/null 2>&1 || {
    echo "[HIL] missing sudo; install permissions are required" >&2
    return 2
  }
  sudo -v
  for entry in "${dependencies[@]}"; do
    cmd="${entry%%:*}"
    package="${entry#*:}"
    if ! command -v "$cmd" >/dev/null 2>&1; then
      if [[ ! " ${missing[*]} " == *" ${package} "* ]]; then
        missing+=("$package")
      fi
    fi
  done
  if ! command -v gdb-multiarch >/dev/null 2>&1 &&
     ! command -v arm-none-eabi-gdb >/dev/null 2>&1; then
    missing+=(gdb-multiarch)
  fi
  if (( ${#missing[@]} > 0 )); then
    echo "[HIL] installing missing host tools: ${missing[*]}"
    command -v apt-get >/dev/null 2>&1 || {
      echo "[HIL] automatic installation requires apt-get" >&2
      return 2
    }
    sudo apt-get update
    sudo apt-get install -y "${missing[@]}"
  fi
  sudo -v
}

spwkit_hil_detect_interface() {
  local requested="${1:-}" candidate
  local -a selected=() fallback=()
  if [[ -n "$requested" ]]; then
    printf '%s\n' "$requested"
    return 0
  fi
  for candidate in /sys/class/net/*; do
    [[ -e "$candidate/device" ]] || continue
    case "${candidate##*/}" in
      wl*|ww*|docker*|veth*|br-*) continue ;;
    esac
    fallback+=("${candidate##*/}")
    [[ -e "$candidate/carrier" ]] || continue
    [[ "$(cat "$candidate/carrier" 2>/dev/null || true)" == "1" ]] || continue
    selected+=("${candidate##*/}")
  done
  if (( ${#selected[@]} == 0 && ${#fallback[@]} == 1 )); then
    echo "[HIL] using the only physical wired NIC (carrier not yet present): ${fallback[0]}" >&2
    selected=("${fallback[0]}")
  fi
  if (( ${#selected[@]} != 1 )); then
    echo "[HIL] cannot uniquely detect connected physical Ethernet NIC" >&2
    echo "[HIL] candidates: ${selected[*]:-(none)}; use --interface NIC" >&2
    return 2
  fi
  printf '%s\n' "${selected[0]}"
}
