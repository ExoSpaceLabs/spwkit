#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

# Shared helpers for hardware/evidence scripts. Public dependencies are pinned
# and prepared automatically so a test run cannot silently use stale sources.

spwkit_prepare_git_checkout() {
  local name="$1"
  local url="$2"
  local revision="$3"
  local destination="$4"
  shift 4

  local parent
  parent="$(dirname "$destination")"
  mkdir -p "$parent"

  if [[ ! -d "$destination/.git" ]]; then
    if [[ -e "$destination" ]]; then
      echo "$name path exists but is not a Git checkout: $destination" >&2
      return 2
    fi
    echo "[deps] cloning $name into $destination"
    git clone --filter=blob:none --no-checkout "$url" "$destination"
  fi

  if [[ $# -gt 0 ]]; then
    git -C "$destination" sparse-checkout init --cone
    git -C "$destination" sparse-checkout set "$@"
  fi

  local current
  current="$(git -C "$destination" rev-parse HEAD 2>/dev/null || true)"
  if [[ "$current" != "$revision" ]]; then
    if [[ -n "$(git -C "$destination" status --porcelain --untracked-files=normal)" ]]; then
      echo "$name checkout has local changes and is not at the pinned revision." >&2
      echo "  path:    $destination" >&2
      echo "  current: ${current:-unknown}" >&2
      echo "  pinned:  $revision" >&2
      echo "Refusing to discard local work. Clean/stash it or remove the checkout." >&2
      return 2
    fi
    echo "[deps] updating $name to pinned revision $revision"
    git -C "$destination" fetch --depth 1 origin "$revision"
    git -C "$destination" checkout --detach "$revision"
  fi

  git -C "$destination" reset --hard "$revision" >/dev/null
  printf '%s\n' "$destination"
}

spwkit_prepare_stm32cubeh7() {
  local destination="$1"
  local revision="$2"
  local url="${3:-https://github.com/STMicroelectronics/STM32CubeH7.git}"

  spwkit_prepare_git_checkout     "STM32CubeH7" "$url" "$revision" "$destination"     Drivers/CMSIS/Include     Drivers/CMSIS/Device/ST/STM32H7xx >/dev/null

  git -C "$destination" submodule update --init --depth 1     Drivers/CMSIS/Device/ST/STM32H7xx

  for file in     "$destination/Drivers/CMSIS/Include/core_cm7.h"     "$destination/Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h755xx.h"; do
    [[ -f "$file" ]] || {
      echo "Incomplete STM32CubeH7 checkout: missing $file" >&2
      return 2
    }
  done

  printf '%s\n' "$destination"
}

spwkit_prepare_das() {
  local destination="$1"
  local revision="$2"
  local url="${3:-https://github.com/Inczert/device-abstraction-stack.git}"

  spwkit_prepare_git_checkout     "DAS" "$url" "$revision" "$destination" >/dev/null

  [[ -f "$destination/cmake/toolchains/arm-none-eabi.cmake" ]] || {
    echo "Incomplete DAS checkout: missing toolchain file in $destination" >&2
    return 2
  }

  printf '%s\n' "$destination"
}
