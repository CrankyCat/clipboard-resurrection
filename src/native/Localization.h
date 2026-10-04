// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace Clipboard::Localization
{
	using Catalog = std::map<std::string, std::string, std::less<>>;
	using Arguments = std::array<std::string_view, 6>;
	using PlaceholderCounts = std::array<std::size_t, 6>;

	inline constexpr std::size_t kMaxCatalogBytes = 4 * 1024 * 1024;
	inline constexpr std::size_t kMaxEntries = 4096;
	inline constexpr std::size_t kMaxKeyBytes = 160;
	inline constexpr std::size_t kMaxValueBytes = 16 * 1024;
	inline constexpr std::string_view kUnavailableText = "Clipboard: text unavailable.";

	struct ParseResult
	{
		Catalog entries;
		std::string error;
		std::size_t line{};
		explicit operator bool() const noexcept { return error.empty(); }
	};

	// Catalogs are UTF-8 key<TAB>value files. Only \\, \n, \r and \t escapes
	// are accepted. A malformed row invalidates the entire file. The key never
	// becomes a path. Formatting accepts {0}..{5} and literal {{ / }} braces.
	[[nodiscard]] bool IsValidUtf8(std::string_view text) noexcept;
	[[nodiscard]] bool IsValidKey(std::string_view key) noexcept;
	[[nodiscard]] bool GetPlaceholderCounts(std::string_view text, PlaceholderCounts& counts) noexcept;
	[[nodiscard]] ParseResult ParseCatalog(std::string_view bytes);
	[[nodiscard]] ParseResult ReadCatalog(const std::filesystem::path& path);
	// Returns one fixed supported catalog suffix, with English as the default.
	// The return value never aliases user-supplied language/path text.
	[[nodiscard]] std::string_view SelectLanguage(std::string_view gameLanguage) noexcept;
	[[nodiscard]] std::string Format(std::string_view text, const Arguments& arguments);
	[[nodiscard]] std::string Resolve(
		const Catalog& english, const Catalog& translated,
		std::string_view key, const Arguments& arguments);
}
