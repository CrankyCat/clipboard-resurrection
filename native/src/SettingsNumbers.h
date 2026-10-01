// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace Clipboard::SettingsNumbers
{

inline std::string_view Trim(std::string_view text) noexcept
{
	constexpr std::string_view whitespace{ " \t\r\n\v\f" };
	const auto first = text.find_first_not_of(whitespace);
	return first == std::string_view::npos ? std::string_view{} :
		text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}

template <class Number>
bool Parse(std::string_view text, Number& number) noexcept
{
	text = Trim(text);
	// from_chars accepts a minus but not the ordinary leading plus accepted by
	// the previous stoi/stof calls. A second sign is still malformed.
	if (!text.empty() && text.front() == '+') {
		text.remove_prefix(1);
		if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
			return false;
		}
	}
	if (text.empty()) {
		return false;
	}
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
	return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

inline std::uint32_t Int(std::string_view text, std::uint32_t fallback) noexcept
{
	std::int32_t number{};
	return Parse(text, number) ? static_cast<std::uint32_t>(number) : fallback;
}

inline float Float(std::string_view text, float fallback) noexcept
{
	float number{};
	return Parse(text, number) && std::isfinite(number) ? number : fallback;
}

}
