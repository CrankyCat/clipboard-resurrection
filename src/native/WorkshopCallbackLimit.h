// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorkshopCallbackState.h"
#include <charconv>
#include <string_view>

namespace Clipboard::WorkshopCallbacks
{
    // -1 preserves the previous normal/single diagnostic policy. Zero means
    // all physical rows may have one ordered request pending, not all phases.
    [[nodiscard]] inline std::int32_t ParseCallbackLimit(std::string_view value) noexcept
    {
        if (value.empty()) { return -1; }
        std::uint32_t number{};
        const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
        return result.ec == std::errc{} && result.ptr == value.data() + value.size() && number <= kMaximumRows ?
            static_cast<std::int32_t>(number) : -1;
    }
}
