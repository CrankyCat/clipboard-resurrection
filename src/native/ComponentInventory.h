// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Clipboard::ComponentInventory
{
    inline constexpr std::uint32_t kSourcesPerBatch = 32;

    // Papyrus has signed 32-bit counts. Saturate instead of wrapping a valid
    // affordable balance negative; payment still verifies actual withdrawals.
    constexpr std::int32_t RepresentCount(std::uint64_t count) noexcept
    {
        return static_cast<std::int32_t>(std::min(count,
            static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())));
    }

    // A saturated single-source count would conceal a real before/after
    // withdrawal delta. Exclude unrepresentable sources instead of charging
    // them and then trying another source for an apparently unpaid balance.
    constexpr std::int32_t UsableSourceCount(std::uint32_t count) noexcept
    {
        return count <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ?
            static_cast<std::int32_t>(count) : -1;
    }

    template <class Sources, class ReadCount>
    std::int32_t SumBatch(const Sources& sources, std::int32_t start, ReadCount&& readCount)
    {
        if (start < 0 || static_cast<std::size_t>(start) >= sources.size()) { return 0; }
        const auto begin = static_cast<std::size_t>(start);
        const auto end = begin + std::min<std::size_t>(kSourcesPerBatch, sources.size() - begin);
        std::uint64_t total = 0;
        for (auto index = begin; index < end; ++index) {
            const auto count = readCount(sources[index]);
            if (count > 0) { total += static_cast<std::uint32_t>(count); }
        }
        return RepresentCount(total);
    }
}
