// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace spwkit {

struct Version {
    std::uint32_t major;
    std::uint32_t minor;
    std::uint32_t patch;
};

inline constexpr Version version{0, 0, 0};

} // namespace spwkit
