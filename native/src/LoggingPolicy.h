// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <string_view>

namespace Clipboard::Logging
{
	inline std::atomic_bool enabled{ false };
	inline std::atomic_bool startup{ false };
	[[nodiscard]] inline bool Enabled() noexcept { return enabled.load(std::memory_order_relaxed); }
	inline void SetEnabled(bool value) noexcept { enabled.store(value, std::memory_order_relaxed); }

	[[nodiscard]] inline bool AllowsImportDetail(bool insideImport, bool verboseImport) noexcept
	{
		return Enabled() && (!insideImport || verboseImport);
	}

	[[nodiscard]] inline bool IsStartupMessage(std::string_view text) noexcept
	{
		if (text == "----- Clipboard session start -----") { return true; }
		if (text.starts_with("Attempting Runtime Database resolution on ") &&
			text.ends_with("; no executable patch whitelist")) { return true; }
		if (text.starts_with("F4RD OK runtime=") && text.find(" mode=") != text.npos &&
			text.find(" source=") != text.npos) { return true; }
		constexpr std::string_view prefix = "Resolved all ";
		constexpr std::string_view suffix = " reviewed CommonLib Runtime Database symbols";
		if (!text.starts_with(prefix) || !text.ends_with(suffix) || text.size() <= prefix.size() + suffix.size()) { return false; }
		const auto count = text.substr(prefix.size(), text.size() - prefix.size() - suffix.size());
		return std::all_of(count.begin(), count.end(), [](char c) { return c >= '0' && c <= '9'; });
	}

	[[nodiscard]] inline bool Allows(std::string_view text, bool info, bool warningOrError = false) noexcept
	{
		// Small deployment identity records are always available, even with routine
		// logging disabled. They do not open a global logging window.
		return warningOrError || Enabled() || (info && text.starts_with("Clipboard build identity: ")) ||
			(startup.load(std::memory_order_relaxed) && info && IsStartupMessage(text));
	}

	class StartupWindow
	{
	public:
		StartupWindow() { startup.store(true, std::memory_order_relaxed); }
		~StartupWindow() { startup.store(false, std::memory_order_relaxed); }
		StartupWindow(const StartupWindow&) = delete;
		StartupWindow& operator=(const StartupWindow&) = delete;
	};
}

// Keep argument construction and formatting out of disabled logging paths.
#define CLIPBOARD_DEBUG_LOG(...) do { if (::Clipboard::Logging::Enabled()) { __VA_ARGS__; } } while (false)

// Legacy parser/row text is filtered before any string construction. Actionable
// warning/error messages bypass both diagnostic macros and remain always on.
#define CLIPBOARD_IMPORT_DETAIL_LOG(insideImport, verboseImport, ...) do { if (::Clipboard::Logging::AllowsImportDetail(insideImport, verboseImport)) { __VA_ARGS__; } } while (false)
