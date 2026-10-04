// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <cstdint>
#include <unordered_map>

namespace Clipboard::ComponentCost
{
	// One bill owns one memo. Preserve the existing resolver's first-match
	// semantics while avoiding another complete COBJ scan for repeated base
	// forms, including forms with no recipe. Never retain this across calls:
	// scripts/mods may change recipes between estimates and later operations.
	// Only the recipe lookup is reused; admission and quantities remain per row.
	template <class Recipe>
	class RecipeMemo
	{
	public:
		template <class Resolve>
		[[nodiscard]] Recipe* Find(std::uint32_t formID, Resolve&& resolve)
		{
			if (!formID) {
				return nullptr;
			}
			const auto found = recipes_.find(formID);
			if (found != recipes_.end()) {
				return found->second;
			}
			auto* recipe = resolve();
			recipes_.emplace(formID, recipe);
			return recipe;
		}

	private:
		std::unordered_map<std::uint32_t, Recipe*> recipes_;
	};
}
