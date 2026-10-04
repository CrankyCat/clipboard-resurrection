// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Clipboard::PatternNumbers
{
    // The shortest fixed representation that round-trips to the source type.
    // Unlike fixed's default six places, this also preserves tiny values and
    // float values that need more digits. Patterns always use an ASCII dot.
    template <class Number>
    std::string Format(Number value)
    {
        char buffer[768];
        const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value,
            std::chars_format::fixed);
        if (error != std::errc{}) { throw std::runtime_error("Pattern number formatting failed"); }
        return { buffer, end };
    }

    template <class Number>
    Number Parse(std::string_view text) noexcept
    {
        const auto first = text.find_first_not_of(" \t\r\n\f\v");
        if (first == text.npos) { return {}; }
        text = text.substr(first, text.find_last_not_of(" \t\r\n\f\v") - first + 1);
        if (text.front() == '+') { text.remove_prefix(1); }
        if (text.empty()) { return {}; }
        Number value{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        return error == std::errc{} && end == text.data() + text.size() && std::isfinite(value) ? value : Number{};
    }

    // Degrees in the file must not be narrowed to float before conversion back
    // to the engine's float radians: that introduces an avoidable second rounding.
    inline float Radians(std::string_view degrees, long double pi) noexcept
    {
        return static_cast<float>(static_cast<long double>(Parse<double>(degrees)) / 180.0L * pi);
    }
}
