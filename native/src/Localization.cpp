// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#include "Localization.h"

#include <cstdint>
#include <fstream>
#include <utility>

namespace Clipboard::Localization
{
	namespace
	{
		[[nodiscard]] ParseResult Failure(std::string message, std::size_t line = 0)
		{
			return { {}, std::move(message), line };
		}

		[[nodiscard]] bool IsAsciiWhitespace(char value) noexcept
		{
			return value == ' ' || value == '\t' || value == '\r' || value == '\n';
		}

		[[nodiscard]] bool EqualsAsciiIgnoreCase(std::string_view left, std::string_view right) noexcept
		{
			if (left.size() != right.size()) {
				return false;
			}
			for (std::size_t i = 0; i < left.size(); ++i) {
				const auto lower = left[i] >= 'A' && left[i] <= 'Z' ? left[i] + ('a' - 'A') : left[i];
				if (lower != right[i]) {
					return false;
				}
			}
			return true;
		}
	}

	bool IsValidUtf8(std::string_view text) noexcept
	{
		for (std::size_t index = 0; index < text.size();) {
			const auto first = static_cast<unsigned char>(text[index++]);
			if (first < 0x80) {
				// Embedded NUL cannot survive BSFixedString/Papyrus transport.
				if (first == 0) {
					return false;
				}
				continue;
			}
			std::size_t continuationCount{};
			std::uint32_t codepoint{};
			std::uint32_t minimum{};
			if (first >= 0xC2 && first <= 0xDF) {
				continuationCount = 1;
				codepoint = first & 0x1Fu;
				minimum = 0x80;
			} else if (first >= 0xE0 && first <= 0xEF) {
				continuationCount = 2;
				codepoint = first & 0x0Fu;
				minimum = 0x800;
			} else if (first >= 0xF0 && first <= 0xF4) {
				continuationCount = 3;
				codepoint = first & 0x07u;
				minimum = 0x10000;
			} else {
				return false;
			}
			if (continuationCount > text.size() - index) {
				return false;
			}
			for (std::size_t continuation = 0; continuation < continuationCount; ++continuation) {
				const auto next = static_cast<unsigned char>(text[index++]);
				if ((next & 0xC0u) != 0x80u) {
					return false;
				}
				codepoint = (codepoint << 6) | (next & 0x3Fu);
			}
			if (codepoint < minimum || codepoint > 0x10FFFF ||
				(codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
				return false;
			}
		}
		return true;
	}

	bool IsValidKey(std::string_view key) noexcept
	{
		constexpr std::string_view prefix = "$Clipboard_";
		if (key.size() <= prefix.size() || key.size() > kMaxKeyBytes || !key.starts_with(prefix)) {
			return false;
		}
		for (const auto value : key.substr(prefix.size())) {
			if (!((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
				(value >= '0' && value <= '9') || value == '_')) {
				return false;
			}
		}
		return true;
	}

	bool GetPlaceholderCounts(std::string_view text, PlaceholderCounts& counts) noexcept
	{
		counts = {};
		for (std::size_t index = 0; index < text.size(); ++index) {
			const auto current = text[index];
			if (current != '{' && current != '}') {
				continue;
			}
			if (index + 1 < text.size() && text[index + 1] == current) {
				++index;
			} else if (current == '{' && index + 2 < text.size() &&
				text[index + 1] >= '0' && text[index + 1] <= '5' && text[index + 2] == '}') {
				++counts[static_cast<std::size_t>(text[index + 1] - '0')];
				index += 2;
			} else {
				return false;
			}
		}
		return true;
	}

	ParseResult ParseCatalog(std::string_view bytes)
	{
		if (bytes.size() > kMaxCatalogBytes) {
			return Failure("catalog exceeds 4 MiB");
		}
		if (bytes.starts_with("\xEF\xBB\xBF")) {
			bytes.remove_prefix(3);
		}
		if (!IsValidUtf8(bytes)) {
			return Failure("catalog is not valid NUL-free UTF-8");
		}
		ParseResult result;
		std::size_t lineNumber = 0;
		while (!bytes.empty()) {
			++lineNumber;
			const auto newline = bytes.find('\n');
			auto line = bytes.substr(0, newline);
			bytes.remove_prefix(newline == std::string_view::npos ? bytes.size() : newline + 1);
			if (!line.empty() && line.back() == '\r') {
				line.remove_suffix(1);
			}
			if (line.empty() || line.front() == '#') {
				continue;
			}
			const auto separator = line.find('\t');
			if (separator == std::string_view::npos || !IsValidKey(line.substr(0, separator))) {
				return Failure("invalid key or missing tab separator", lineNumber);
			}
			const auto key = line.substr(0, separator);
			const auto encoded = line.substr(separator + 1);
			if (encoded.empty() || encoded.size() > 2 * kMaxValueBytes) {
				return Failure("empty or oversized text", lineNumber);
			}
			std::string value;
			value.reserve(encoded.size());
			for (std::size_t index = 0; index < encoded.size(); ++index) {
				const auto character = encoded[index];
				if (static_cast<unsigned char>(character) < 0x20 || character == 0x7F) {
					return Failure("unescaped control character in text", lineNumber);
				}
				if (character != '\\') {
					value.push_back(character);
					continue;
				}
				if (++index == encoded.size()) {
					return Failure("unfinished escape", lineNumber);
				}
				switch (encoded[index]) {
				case '\\': value.push_back('\\'); break;
				case 'n': value.push_back('\n'); break;
				case 'r': value.push_back('\r'); break;
				case 't': value.push_back('\t'); break;
				default: return Failure("unknown escape", lineNumber);
				}
			}
			PlaceholderCounts counts;
			if (value.size() > kMaxValueBytes || !GetPlaceholderCounts(value, counts)) {
				return Failure("oversized text or invalid placeholder/braces", lineNumber);
			}
			if (!result.entries.emplace(std::string(key), std::move(value)).second) {
				return Failure("duplicate key", lineNumber);
			}
			if (result.entries.size() > kMaxEntries) {
				return Failure("catalog exceeds 4096 entries", lineNumber);
			}
		}
		if (result.entries.empty()) {
			return Failure("catalog has no entries");
		}
		return result;
	}

	ParseResult ReadCatalog(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		if (!input) {
			return Failure("catalog could not be opened");
		}
		const auto length = input.tellg();
		if (length < 0 || length > static_cast<std::streamoff>(kMaxCatalogBytes)) {
			return Failure("catalog size could not be read or exceeds 4 MiB");
		}
		std::string bytes(static_cast<std::size_t>(length), '\0');
		input.seekg(0);
		if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) ||
			input.peek() != std::char_traits<char>::eof()) {
			return Failure("catalog read failed or file size changed while reading");
		}
		return ParseCatalog(bytes);
	}

	std::string_view SelectLanguage(std::string_view gameLanguage) noexcept
	{
		while (!gameLanguage.empty() && IsAsciiWhitespace(gameLanguage.front())) {
			gameLanguage.remove_prefix(1);
		}
		while (!gameLanguage.empty() && IsAsciiWhitespace(gameLanguage.back())) {
			gameLanguage.remove_suffix(1);
		}
		// Return only fixed catalog suffixes, never the supplied setting value.
		constexpr std::array<std::string_view, 12> languages{
			"en", "ru", "de", "es", "esmx", "fr", "it", "ja", "pl", "ptbr", "zhhans", "zhhant"
		};
		for (const auto language : languages) {
			if (EqualsAsciiIgnoreCase(gameLanguage, language)) {
				return language;
			}
		}
		// Fallout 4's Traditional Chinese archives use the legacy cn suffix.
		if (EqualsAsciiIgnoreCase(gameLanguage, "cn")) {
			return "zhhant";
		}
		return EqualsAsciiIgnoreCase(gameLanguage, "russian") ? "ru" : "en";
	}

	std::string Format(std::string_view text, const Arguments& arguments)
	{
		std::string result;
		result.reserve(text.size());
		for (std::size_t index = 0; index < text.size(); ++index) {
			const auto current = text[index];
			if ((current == '{' || current == '}') && index + 1 < text.size() && text[index + 1] == current) {
				result.push_back(current);
				++index;
			} else if (current == '{' && index + 2 < text.size() &&
				text[index + 1] >= '0' && text[index + 1] <= '5' && text[index + 2] == '}') {
				// Append directly: a user-supplied name containing {1}, markup or
				// backslashes must never be parsed as another template/escape.
				result.append(arguments[static_cast<std::size_t>(text[index + 1] - '0')]);
				index += 2;
			} else {
				result.push_back(current);
			}
		}
		return result;
	}

	std::string Resolve(const Catalog& english, const Catalog& translated, std::string_view key, const Arguments& arguments)
	{
		const auto source = english.find(key);
		if (source == english.end()) {
			return std::string(kUnavailableText);
		}
		auto text = std::string_view(source->second);
		if (const auto localized = translated.find(key); localized != translated.end()) {
			PlaceholderCounts sourceCounts;
			PlaceholderCounts localizedCounts;
			if (GetPlaceholderCounts(text, sourceCounts) &&
				GetPlaceholderCounts(localized->second, localizedCounts) && sourceCounts == localizedCounts) {
				text = localized->second;
			}
		}
		return Format(text, arguments);
	}
}
