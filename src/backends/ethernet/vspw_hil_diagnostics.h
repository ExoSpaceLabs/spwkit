// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_VSPW_HIL_DIAGNOSTICS_H
#define SPWKIT_VSPW_HIL_DIAGNOSTICS_H
/* Private source-only HIL snapshot. Not installed; not a stable C API. */
#include <spwkit/port.h>
#include "backends/ethernet/vspw_engine.h"
#ifdef __cplusplus
extern "C" {
#endif
spw_result_t spwkit_private_vspw_diagnostics(
    const spw_port_t* port, spw_vspw_diagnostics_t* out);
#ifdef __cplusplus
}
#endif
#endif
