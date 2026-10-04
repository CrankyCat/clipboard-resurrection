// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ClipboardInputState.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>

namespace Clipboard::ScaleConstraints
{
    inline constexpr std::uint64_t kMicros = 1000000;
    inline constexpr std::uint64_t kHundredPercent = 100 * kMicros;

    struct Ratio
    {
        std::uint64_t numerator{}, denominator{1};
        explicit operator bool() const noexcept { return numerator != 0; }
        float Factor() const noexcept { return static_cast<float>(numerator) / static_cast<float>(denominator); }
        std::uint32_t Target(std::uint32_t percent) const noexcept
        {
            return static_cast<std::uint32_t>(percent * numerator / denominator);
        }
    };

    struct Alternatives { std::uint64_t step{}, maximum{}, lower{}, upper{}; };

    // Summarize actual engine integer percentages, never reconstructed floats.
    struct Selection
    {
        std::uint32_t divisor{}, minimum{1000}, maximum{};
        bool valid{true};

        void Add(std::uint32_t percent) noexcept
        {
            if (percent < 1 || percent > 1000) { valid = false; return; }
            divisor = std::gcd(divisor, percent);
            minimum = (std::min)(minimum, percent);
            maximum = (std::max)(maximum, percent);
        }
        explicit operator bool() const noexcept { return valid && divisor != 0; }

        Ratio CheckInput(std::string_view text, bool increase) const noexcept
        {
            Input::Request request;
            request.inputType = 1;
            request.maxChars = 64;
            request.minimum = 0;
            request.maximum = increase ? 1000 : 99;
            const auto parsed = Input::Validate(request, text);
            if (!*this || !parsed.valid || parsed.empty) { return {}; }
            const auto numerator = increase ? kHundredPercent + parsed.decimalMicros :
                kHundredPercent - parsed.decimalMicros;
            if (divisor * numerator % kHundredPercent != 0 ||
                minimum * numerator < kHundredPercent || maximum * numerator > 1000 * kHundredPercent) {
                return {};
            }
            return { numerator, kHundredPercent };
        }

        // Existing Papyrus/saved-call signatures carry a float factor. Recover
        // its exact common ratio within conversion error only. Typed input is
        // checked above BEFORE this conversion; near-valid decimal text fails.
        Ratio CheckFactor(float factor) const noexcept
        {
            if (!*this || !std::isfinite(factor) || factor <= 0 || factor > 11.00001F) { return {}; }
            const auto numerator = static_cast<std::uint64_t>(std::llround(static_cast<double>(factor) * divisor));
            const Ratio ratio{ numerator, divisor };
            const auto tolerance = 4 * std::numeric_limits<float>::epsilon() * (std::max)(1.0F, factor);
            if (!ratio || std::abs(factor - ratio.Factor()) > tolerance ||
                minimum * numerator < divisor || maximum * numerator > 1000ULL * divisor ||
                numerator * 100 < divisor || numerator > 11ULL * divisor) { return {}; }
            return ratio;
        }

        Alternatives Choices(std::uint64_t requested, bool increase) const noexcept
        {
            if (!*this) { return {}; }
            // Restrict suggestions to exact decimals the six-place input can
            // express. For 150%, 0.666666% is not an exact 1-point increase.
            const auto step = kHundredPercent / std::gcd<std::uint64_t>(divisor, kHundredPercent);
            const auto limit = increase ? (std::min)(1000 * kMicros,
                (1000 - maximum) * kHundredPercent / maximum) :
                (std::min)(99 * kMicros, (minimum - 1) * kHundredPercent / minimum);
            const auto last = limit / step * step;
            if (!last) { return {step, 0, 0, 0}; }
            const auto lower = (std::min)(requested / step * step, last);
            const auto upper = lower < last ? lower + step : 0;
            return {step, last, lower, upper};
        }
    };

    inline std::string PercentText(std::uint64_t micros)
    {
        auto text = std::to_string(micros / kMicros);
        if (const auto fraction = micros % kMicros) {
            auto digits = std::to_string(kMicros + fraction).substr(1);
            while (digits.back() == '0') { digits.pop_back(); }
            text += '.' + digits;
        }
        return text;
    }
}
