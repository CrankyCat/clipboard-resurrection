// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "MasterPluginBlacklist.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Clipboard::OptionalFiltering::Policy
{
	struct FormSpec
	{
		std::string plugin;
		std::uint32_t localFormID;
	};

	[[nodiscard]] inline std::string_view Trim(std::string_view value)
	{
		constexpr std::string_view whitespace{ " \t\r\n\f\v" };
		const auto first = value.find_first_not_of(whitespace);
		if (first == std::string_view::npos) {
			return {};
		}
		const auto last = value.find_last_not_of(whitespace);
		return value.substr(first, last - first + 1);
	}

	// The existing configuration convention is a decimal local FormID. Never
	// accept a numeric prefix of a malformed token or a runtime load-order ID.
	// Empty CSV entries are ignored by the caller, not resolved as form zero.
	[[nodiscard]] inline std::optional<FormSpec> ParseFormSpec(std::string_view value)
	{
		value = Trim(value);
		const auto separator = value.find('#');
		if (separator == std::string_view::npos ||
			value.find('#', separator + 1) != std::string_view::npos ||
			value.find('\0') != std::string_view::npos) {
			return std::nullopt;
		}
		const auto plugin = Trim(value.substr(0, separator));
		const auto decimal = Trim(value.substr(separator + 1));
		if (plugin.empty() || decimal.empty() ||
			!std::all_of(decimal.begin(), decimal.end(), [](char digit) {
				return digit >= '0' && digit <= '9';
			})) {
			return std::nullopt;
		}
		std::uint32_t localFormID = 0;
		const auto parsed = std::from_chars(decimal.data(), decimal.data() + decimal.size(), localFormID, 10);
		if (parsed.ec != std::errc{} || parsed.ptr != decimal.data() + decimal.size() ||
			localFormID == 0 || localFormID > 0x00FFFFFF) {
			return std::nullopt;
		}
		return FormSpec{ std::string{ plugin }, localFormID };
	}

	[[nodiscard]] inline bool EqualNames(std::string_view left, std::string_view right)
	{
		return left.size() == right.size() &&
			std::equal(left.begin(), left.end(), right.begin(), [](unsigned char first, unsigned char second) {
				return std::tolower(first) == std::tolower(second);
			});
	}

	// The configured roots themselves and their active direct dependents match.
	// Do not recursively widen the existing master-table contract. This helper
	// accepts a TESFile or the host-test equivalent without engine allocation.
	template <class File>
	[[nodiscard]] bool MatchesPluginFamily(const File* file, std::span<const std::string> roots)
	{
		if (!file || !file->IsActive() || roots.empty()) {
			return false;
		}
		for (const auto& root : roots) {
			if (!root.empty() && EqualNames(file->GetFilename(), root)) {
				return true;
			}
		}
		if (!file->masterPtrs) {
			return false;
		}
		for (std::uint32_t index = 0; index < file->masterCount; ++index) {
			const auto* master = file->masterPtrs[index];
			if (!master || !master->IsActive()) {
				continue;
			}
			// Keep the established exact-name matcher, exposing only an active
			// direct master. Resolved but inactive entries are not dependencies
			// of the current loaded family for this optional filter.
			const File* directMaster[]{ master };
			struct ActiveDirectMaster
			{
				const File* const* masterPtrs;
				std::uint32_t masterCount{ 1 };
				bool IsActive() const { return true; }
			};
			const ActiveDirectMaster view{ directMaster };
			if (!Blacklist::FindBlacklistedMaster(&view, roots).empty()) {
				return true;
			}
		}
		return false;
	}

	inline constexpr std::string_view kDefaultReferenceKeywordsCsv{
		"SS2.esm#84480,Fallout4.esm#2391846"
	};
	inline constexpr std::string_view kDefaultCollectionPluginFamiliesCsv{ "SS2.esm" };
	inline constexpr std::string_view kDefaultAutoBedsParentScriptsCsv{
		"ukAutoBeds:ukTablePlaceAtMe,ukAutoBeds:ukMultiPlaceAtMe"
	};

	// Union of SlotKeywords on the 34 ukTablePlaceAtMe/ukMultiPlaceAtMe roots
	// in the inspected ukAutoBeds.esp (SHA-256 3a77c807df5e2cc67f240e4838372a0b4
	// e0ac460dbef8d692ee0bc5804659777). These keys only nominate links to inspect;
	// a supported parent and its current outgoing slot link must still match.
	inline constexpr std::string_view kDefaultAutoBedsSlotKeywordsCsv{
		"ukAutoBeds.esp#37038,ukAutoBeds.esp#37039,ukAutoBeds.esp#247965,"
		"ukAutoBeds.esp#300450,ukAutoBeds.esp#300465,ukAutoBeds.esp#300472,"
		"ukAutoBeds.esp#300477,ukAutoBeds.esp#335602,ukAutoBeds.esp#510781,"
		"ukAutoBeds.esp#528283,ukAutoBeds.esp#563313,ukAutoBeds.esp#615877,"
		"ukAutoBeds.esp#615878,ukAutoBeds.esp#615879,ukAutoBeds.esp#615880,"
		"ukAutoBeds.esp#615881,ukAutoBeds.esp#615882,ukAutoBeds.esp#615884,"
		"ukAutoBeds.esp#615885,ukAutoBeds.esp#615886,ukAutoBeds.esp#615887,"
		"ukAutoBeds.esp#931059,ukAutoBeds.esp#1421151,ukAutoBeds.esp#1438678,"
		"ukAutoBeds.esp#1473891,ukAutoBeds.esp#1473892,ukAutoBeds.esp#1473893,"
		"ukAutoBeds.esp#1473894,ukAutoBeds.esp#1473895,ukAutoBeds.esp#1473896,"
		"ukAutoBeds.esp#1473897,ukAutoBeds.esp#1473898,ukAutoBeds.esp#1473899,"
		"ukAutoBeds.esp#1473900,ukAutoBeds.esp#1508942,ukAutoBeds.esp#1508943,"
		"ukAutoBeds.esp#1508944,ukAutoBeds.esp#1543978,ukAutoBeds.esp#1543979,"
		"ukAutoBeds.esp#1543980,ukAutoBeds.esp#1543981,ukAutoBeds.esp#1543983"
	};
}

namespace Clipboard::OptionalFiltering
{
	inline constexpr const char* kDefaultChildLinkKeywords = Policy::kDefaultAutoBedsSlotKeywordsCsv.data();
	inline constexpr const char* kDefaultChildParentScripts = Policy::kDefaultAutoBedsParentScriptsCsv.data();
}
