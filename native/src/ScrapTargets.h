// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <cstddef>
#include <map>
#include <vector>

namespace Clipboard::Scrapping
{

// The batch retains each selected reference and each distinct workshop before
// destruction. A workshop shared by many targets must not gain one intrusive
// hold per target: the engine's BSHandleRefObject counter has only ten bits.
template <class RetainedReference>
struct Target
{
	RetainedReference reference;
	typename RetainedReference::element_type* workshop{};
};

template <class RetainedReference>
class Batch
{
public:
	using Reference = typename RetainedReference::element_type;
	using TargetsType = std::vector<Target<RetainedReference>>;

	Batch() = default;
	Batch(const Batch&) = delete;
	Batch& operator=(const Batch&) = delete;
	Batch(Batch&&) noexcept = default;
	Batch& operator=(Batch&&) = delete;

	void Reserve(std::size_t count) { targets.reserve(count); }

	// The caller supplies distinct selected references. Construct the owner's
	// intrusive pointer only on first insertion, including for a shared owner.
	void Add(Reference* reference, Reference* workshop)
	{
		if (workshop) {
			workshops.try_emplace(workshop, workshop);
		}
		targets.push_back({ RetainedReference{ reference }, workshop });
	}

	[[nodiscard]] TargetsType& Targets() noexcept { return targets; }
	[[nodiscard]] const TargetsType& Targets() const noexcept { return targets; }

private:
	// Reverse member destruction releases targets before their workshop owners.
	std::map<Reference*, RetainedReference> workshops;
	TargetsType targets;
};

template <class Targets, class IsDeleted, class Dispatch>
std::size_t DispatchLiveTargets(const Targets& targets, IsDeleted&& isDeleted, Dispatch&& dispatch)
{
	std::size_t dispatched{};
	for (const auto& target : targets) {
		if (!target.reference || isDeleted(target.reference.get())) {
			continue;
		}
		auto* workshop = target.workshop;
		if (workshop && isDeleted(workshop)) {
			workshop = nullptr;
		}
		if (dispatch(target.reference.get(), workshop)) {
			++dispatched;
		}
	}
	return dispatched;
}

}
