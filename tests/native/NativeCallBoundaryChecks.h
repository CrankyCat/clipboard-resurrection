// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include "NativeCallBoundary.h"

#include <array>
#include <stdexcept>
#include <string>

namespace Clipboard::NativeCallBoundaryChecks
{
	template <class Check>
	void Run(Check&& check)
	{
		int result = -1;
		int reports = 0;
		std::string diagnostic;
		const auto failure = [&](const char* message) {
			result = -1;
			++reports;
			diagnostic = message;
		};
		check(NativeCallBoundary::Invoke([&] { result = 42; return true; }, failure) &&
			result == 42 && reports == 0, "successful native dispatch preserves its result");
		check(!NativeCallBoundary::Invoke([] { return false; }, failure) && reports == 0,
			"native rejection is not converted to success or reported as an exception");

		// Fail each part of the same dispatch lifetime. Later work must not run,
		// and a value partially packed before failure must not reach Papyrus.
		for (int failAt = 0; failAt != 3; ++failAt) {
			std::array<int, 3> visits{};
			check(!NativeCallBoundary::Invoke([&] {
				for (int stage = 0; stage != 3; ++stage) {
					++visits[stage];
					result = stage;
					if (stage == failAt) { throw std::invalid_argument("invalid native input"); }
				}
				return true;
			}, failure), "unpacking, native execution or packing failure rejects dispatch");
			check(result == -1 && diagnostic == "invalid native input",
				"exception clears the partial return and preserves its diagnostic");
			for (int stage = 0; stage != 3; ++stage) {
				check(visits[stage] == (stage <= failAt ? 1 : 0),
					"dispatch stops at the throwing stage without replaying side effects");
			}
		}
		check(!NativeCallBoundary::Invoke([]() -> bool { throw 7; }, failure) &&
			diagnostic == "unknown C++ exception", "unknown C++ exceptions cannot cross the VM boundary");
		check(!NativeCallBoundary::Invoke([]() -> bool { throw std::bad_alloc{}; },
			[](const char*) { throw std::runtime_error("diagnostic failed"); }),
			"failure reporting cannot throw through the VM boundary");
	}
}
