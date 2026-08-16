// SPDX-License-Identifier: Apache-2.0
#pragma once

/*
 * Transitional C++ include used by the v0.2 simulator while the runtime is
 * migrated to the C11 backend contract. The storage layout itself is owned by
 * the language-neutral C header so C and C++ translation units see one opaque
 * spw_buffer representation.
 */
#include "core/buffer_internal.h"
