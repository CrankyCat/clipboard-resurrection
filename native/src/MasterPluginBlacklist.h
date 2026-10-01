// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Clipboard::Blacklist
{
	// File is TESFile in game. Only its direct, resolved master table is read;
	// the configured roots and transitive dependencies are not added implicitly.
	template <class File>
	[[nodiscard]] std::string_view FindBlacklistedMaster(
		const File* file, std::span<const std::string> blacklistedMasters)
	{
		if (!file || !file->IsActive() || !file->masterPtrs || blacklistedMasters.empty()) {
			return {};
		}
		for (std::uint32_t index = 0; index < file->masterCount; ++index) {
			const auto* master = file->masterPtrs[index];
			if (!master) {
				continue;
			}
			const auto name = master->GetFilename();
			for (const auto& entry : blacklistedMasters) {
				if (!entry.empty() && entry.size() == name.size() &&
					std::equal(entry.begin(), entry.end(), name.begin(), [](unsigned char left, unsigned char right) {
						return std::tolower(left) == std::tolower(right);
					})) {
					return name;
				}
			}
		}
		return {};
	}
}
