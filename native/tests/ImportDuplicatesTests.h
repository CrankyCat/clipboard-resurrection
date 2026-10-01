// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once
#include "ImportDuplicates.h"
#include <limits>

template <class Check>
void CheckImportDuplicates(Check&& check)
{
	using namespace Clipboard::ImportDuplicates;
	const Transform original{ { -45001.125, 1920, -72.5 }, { 0.1, -0.2, 0.3 }, 0.75 };
	check(Matches(original, original), "identical full transforms match");
	for (std::size_t i = 0; i < 3; ++i) {
		auto other = original;
		other.position[i] += 0.09;
		check(Matches(original, other), "saved position rounding is tolerated on each axis");
		other.position[i] += 0.02;
		check(!Matches(original, other), "nearby pieces beyond rounding allowance stay distinct");
		other = original;
		other.angle[i] += 2 * std::numbers::pi;
		check(Matches(original, other), "equivalent wrapped angle matches");
		other.angle[i] += 0.001;
		check(!Matches(original, other), "rotated pieces sharing an origin stay distinct");
		other = original;
		other.position[i] = std::numeric_limits<double>::infinity();
		check(!Matches(original, other), "non-finite positions never match");
		other = original;
		other.angle[i] = std::numeric_limits<double>::quiet_NaN();
		check(!Matches(original, other), "non-finite angles never match");
	}
	auto scaled = original;
	scaled.scale += 0.01;
	check(!Matches(original, scaled), "different scale remains a new object");
	scaled.scale = 0;
	check(!Matches(scaled, scaled), "invalid scale cannot match");
	Index<int> index;
	index.Add(1, original, 10);
	index.Add(1, original, 20);
	check(index.Find(2, original, [](int) { return true; }) == nullptr, "same transform with another base never matches");
	check(*index.Find(1, original, [](int) { return true; }) == 10, "stable first accepted duplicate wins");
	check(*index.Find(1, original, [](int id) { return id == 20; }) == 20, "deleted disabled moved or foreign-space candidate can be rejected live");
	check(index.Find(1, original, [](int) { return false; }) == nullptr, "no stale candidate is returned when all validations fail");
	// Large row counts and identical X coordinates must preserve all candidates.
	Index<int> large;
	for (int i = 0; i < 6461; ++i) {
		auto transform = original;
		transform.position[1] = i;
		large.Add(1, transform, i);
	}
	auto last = original;
	last.position[1] = 6460;
	check(*large.Find(1, last, [](int) { return true; }) == 6460, "large patterns retain the final candidate without row truncation");
}
