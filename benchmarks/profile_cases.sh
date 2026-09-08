#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

# Central registry for hosted software profiling cases. Both local/manual
# campaigns and GitHub Actions source this file so probe-pair definitions do
# not drift between execution paths.

SPWKIT_PROFILE_CASES=(
  tx_api_backend
  tx_backend_provider
  tx_provider_native
  tx_api_native
)

spw_profile_case_resolve() {
  local case_name="$1"
  case "$case_name" in
    tx_api_backend)
      SPW_PROFILE_CASE_START="SPW_PROFILE_ID_TX_API_ENTRY"
      SPW_PROFILE_CASE_END="SPW_PROFILE_ID_TX_BACKEND_ENTRY"
      ;;
    tx_backend_provider)
      SPW_PROFILE_CASE_START="SPW_PROFILE_ID_TX_BACKEND_ENTRY"
      SPW_PROFILE_CASE_END="SPW_PROFILE_ID_TX_PROVIDER_ENTRY"
      ;;
    tx_provider_native)
      SPW_PROFILE_CASE_START="SPW_PROFILE_ID_TX_PROVIDER_ENTRY"
      SPW_PROFILE_CASE_END="SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY"
      ;;
    tx_api_native)
      SPW_PROFILE_CASE_START="SPW_PROFILE_ID_TX_API_ENTRY"
      SPW_PROFILE_CASE_END="SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY"
      ;;
    *)
      return 1
      ;;
  esac
}

spw_profile_case_known() {
  spw_profile_case_resolve "$1" >/dev/null 2>&1
}

spw_profile_case_list() {
  printf '%s\n' "${SPWKIT_PROFILE_CASES[@]}"
}
