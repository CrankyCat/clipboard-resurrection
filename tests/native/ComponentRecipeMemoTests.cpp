// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#include "ComponentRecipeMemo.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
	struct Recipe
	{
		std::vector<std::uint32_t> createdForms;
		std::uint32_t quantity;
		bool deleted{};
	};

	// Test fixture for the existing first-live-match resolver. Production still
	// uses its unchanged engine resolver; this supplies observable lookup counts
	// and direct/list-created forms to exercise the production memo boundary.
	Recipe* Resolve(std::vector<Recipe>& recipes, std::uint32_t formID,
		std::uint32_t& searches)
	{
		++searches;
		for (auto& recipe : recipes) {
			if (!recipe.deleted && std::find(recipe.createdForms.begin(),
					recipe.createdForms.end(), formID) != recipe.createdForms.end()) {
				return &recipe;
			}
		}
		return nullptr;
	}
}

int main()
{
	int checks{}, failures{};
	const auto check = [&](bool success, const char* name) {
		++checks;
		if (!success) {
			++failures;
			std::cerr << name << '\n';
		}
	};
	using Clipboard::ComponentCost::RecipeMemo;
	std::vector<Recipe> recipes{
		{{10}, 99, true}, {{10, 20}, 3}, {{10}, 7}, {{30}, 2}
	};
	std::uint32_t searches{};
	RecipeMemo<Recipe> memo;
	const auto find = [&](std::uint32_t id) {
		return memo.Find(id, [&] { return Resolve(recipes, id, searches); });
	};
	check(find(10) == &recipes[1], "deleted recipe skipped and first surviving FLST match preserved");
	check(find(10) == &recipes[1] && searches == 1, "repeated base retains first match without another lookup");
	check(find(20) == &recipes[1] && searches == 2, "different FLST member resolves independently");
	check(find(30) == &recipes[3] && searches == 3, "ordinary created-form lookup is preserved");
	check(find(40) == nullptr && searches == 4, "absent recipe is a valid miss");
	check(find(40) == nullptr && searches == 4, "negative lookup is memoized");
	check(find(0) == nullptr && searches == 4, "invalid form identity cannot dispatch resolver");

	// Compare an admitted bill with and without memoization. Every row still
	// evaluates admission, and each admitted repeated row adds its full quantity.
	// Include both hits and misses to prevent a positive-only cache regression.
	RecipeMemo<Recipe> repeatedBill;
	std::uint32_t baselineSearches{}, memoSearches{}, baselineTotal{}, memoTotal{};
	std::uint32_t baselineAdmissionChecks{}, memoAdmissionChecks{};
	constexpr std::array<std::uint32_t, 5> forms{10, 20, 30, 40, 10};
	for (std::uint32_t row = 0; row < 10000; ++row) {
		const auto id = forms[row % forms.size()];
		const bool admitted = row % 7 != 0;
		++baselineAdmissionChecks;
		if (admitted) {
			if (const auto* recipe = Resolve(recipes, id, baselineSearches)) {
				baselineTotal += recipe->quantity;
			}
		}
		++memoAdmissionChecks;
		if (admitted) {
			if (const auto* recipe = repeatedBill.Find(id, [&] {
					return Resolve(recipes, id, memoSearches);
				})) {
				memoTotal += recipe->quantity;
			}
		}
	}
	check(memoTotal == baselineTotal && memoTotal == 18854,
		"10000-row bill preserves duplicate quantities and omitted rows");
	check(baselineAdmissionChecks == 10000 && memoAdmissionChecks == baselineAdmissionChecks,
		"admission is checked independently for every row");
	check(baselineSearches == 8571 && memoSearches == 4,
		"recipe scans scale with distinct admitted forms rather than row count");

	// No result survives the operation. A later estimate sees both new misses
	// becoming hits and changes to first-live recipe precedence.
	recipes[1].deleted = true;
	recipes.push_back({{40}, 8});
	RecipeMemo<Recipe> nextBill;
	check(nextBill.Find(10, [&] { return Resolve(recipes, 10, searches); }) == &recipes[2],
		"new operation observes changed recipe precedence");
	check(nextBill.Find(40, [&] { return Resolve(recipes, 40, searches); }) == &recipes[4],
		"negative result does not outlive its operation");

	// A failed lookup must not poison the memo with a manufactured miss.
	RecipeMemo<Recipe> retry;
	try {
		(void)retry.Find(30, []() -> Recipe* { throw std::runtime_error("fixture"); });
		check(false, "resolver exception propagates");
	}
	catch (const std::runtime_error&) {
		check(true, "resolver exception propagates");
	}
	check(retry.Find(30, [&] { return Resolve(recipes, 30, searches); }) == &recipes[3],
		"unsuccessful lookup leaves no memo entry");

	std::cout << checks << " component recipe memo checks, " << failures << " failures; "
		<< "synthetic 10000-row resolver calls " << baselineSearches << " -> " << memoSearches
		<< ", total quantity " << memoTotal << '\n';
	return failures ? 1 : 0;
}
