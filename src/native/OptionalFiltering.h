// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace RE
{
	class TESForm;
	class TESObjectREFR;
}

namespace Clipboard::OptionalFiltering
{
	void Configure(
		const std::vector<std::string>& referenceKeywords,
		const std::vector<std::string>& scolFamilyRoots,
		const std::vector<std::string>& childLinkKeywords,
		const std::vector<std::string>& parentScripts);

	// Nesting shares one operation-local parent/child index. The outermost
	// destructor drops every retained reference; nothing survives a scan/load.
	class ScopedScan
	{
	public:
		ScopedScan();
		~ScopedScan();
		ScopedScan(const ScopedScan&) = delete;
		ScopedScan& operator=(const ScopedScan&) = delete;
		ScopedScan(ScopedScan&&) = delete;
		ScopedScan& operator=(ScopedScan&&) = delete;
	};

	[[nodiscard]] bool IsBlocked(const RE::TESObjectREFR* reference);
	[[nodiscard]] bool IsBlockedCollection(const RE::TESForm* baseForm);
}
