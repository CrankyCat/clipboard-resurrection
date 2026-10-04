// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include "ScrapTargets.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <type_traits>
#include <utility>
#include <vector>

namespace Clipboard::Scrapping::Tests
{

// Keep the fixture objects alive independently of the modeled intrusive count.
// A zero low-ten-bit count records the engine's destruction attempt without
// freeing memory, so the historical overflow can be demonstrated without UB.
struct Reference
{
	explicit Reference(unsigned a_id, unsigned a_initialHolds = 1) :
		id(a_id), packedCount(a_initialHolds), liveHolds(a_initialHolds), peakHolds(a_initialHolds)
	{}

	void Retain()
	{
		++packedCount;
		++liveHolds;
		peakHolds = std::max(peakHolds, liveHolds);
	}

	void Release()
	{
		if (liveHolds == 0) {
			++unbalancedReleases;
			return;
		}
		--liveHolds;
		if ((--packedCount & 0x3FFU) == 0) {
			if (liveHolds != 0) {
				++prematureDestructions;
			} else {
				++legitimateDestructions;
				if (destructionOrder) {
					destructionOrder->push_back(id);
				}
			}
		}
	}

	bool BalancedAt(unsigned a_expectedHolds) const
	{
		return packedCount == a_expectedHolds && liveHolds == a_expectedHolds &&
			prematureDestructions == 0 && unbalancedReleases == 0;
	}

	unsigned id;
	bool deleted{};
	std::uint32_t packedCount;
	unsigned liveHolds;
	unsigned peakHolds;
	unsigned prematureDestructions{};
	unsigned legitimateDestructions{};
	unsigned unbalancedReleases{};
	std::vector<unsigned>* destructionOrder{};
};

class RetainedReference
{
public:
	using element_type = Reference;

	RetainedReference() = default;
	explicit RetainedReference(Reference* a_reference) : _reference(a_reference)
	{
		if (_reference) {
			_reference->Retain();
		}
	}
	RetainedReference(const RetainedReference& a_other) : RetainedReference(a_other._reference) {}
	RetainedReference(RetainedReference&& a_other) noexcept :
		_reference(std::exchange(a_other._reference, nullptr))
	{}
	RetainedReference& operator=(RetainedReference a_other) noexcept
	{
		std::swap(_reference, a_other._reference);
		return *this;
	}
	~RetainedReference() { reset(); }

	Reference* get() const { return _reference; }
	Reference* operator->() const { return _reference; }
	explicit operator bool() const { return _reference != nullptr; }
	void reset()
	{
		if (auto* reference = std::exchange(_reference, nullptr)) {
			reference->Release();
		}
	}

private:
	Reference* _reference{};
};

using TestBatch = Batch<RetainedReference>;
static_assert(!std::is_copy_constructible_v<TestBatch>);
static_assert(!std::is_copy_assignable_v<TestBatch>);
static_assert(std::is_nothrow_move_constructible_v<TestBatch>);
static_assert(!std::is_move_assignable_v<TestBatch>);

inline bool TestLegacyOverflowModel()
{
	Reference workshop{ 100, 8 };
	{
		std::vector<RetainedReference> duplicateOwners;
		duplicateOwners.reserve(1058);
		for (unsigned i = 0; i < 1058; ++i) {
			duplicateOwners.emplace_back(&workshop);
		}
		if (workshop.liveHolds != 1066 || workshop.peakHolds != 1066 ||
			workshop.prematureDestructions != 0) {
			std::cerr << "Scrap counter fixture did not reproduce legacy owner acquisition\n";
			return false;
		}
	}
	if (workshop.liveHolds != 8 || workshop.packedCount != 8 ||
		workshop.prematureDestructions != 1 || workshop.legitimateDestructions != 0 ||
		workshop.unbalancedReleases != 0) {
		std::cerr << "Scrap counter fixture missed the legacy premature zero with live holds\n";
		return false;
	}
	return true;
}

inline bool TestLargeSharedWorkshop(unsigned a_count)
{
	Reference workshop{ a_count + 1, 8 };
	std::vector<Reference> references;
	references.reserve(a_count);
	for (unsigned i = 0; i < a_count; ++i) {
		references.emplace_back(a_count - i);
	}
	{
		TestBatch batch;
		batch.Reserve(a_count);
		for (auto& reference : references) {
			batch.Add(&reference, &workshop);
		}
		if (!workshop.BalancedAt(9) || workshop.peakHolds != 9 ||
			batch.Targets().size() != a_count) {
			std::cerr << "Large scrap batch retained its shared workshop more than once\n";
			return false;
		}
		auto& targets = batch.Targets();
		std::sort(targets.begin(), targets.end(), [](const auto& a_left, const auto& a_right) {
			return a_left.reference->id < a_right.reference->id;
		});
		const TestBatch moved{ std::move(batch) };
		unsigned nextID = 1;
		bool validDispatch = moved.Targets().size() == a_count;
		const auto count = DispatchLiveTargets(moved.Targets(),
			[](const Reference* reference) { return reference->deleted; },
			[&](Reference* reference, Reference* owner) {
				validDispatch = validDispatch && reference->id == nextID++ &&
					reference->BalancedAt(2) && owner == &workshop && workshop.BalancedAt(9);
				return true;
			});
		if (!validDispatch || count != a_count || nextID != a_count + 1 || workshop.peakHolds != 9) {
			std::cerr << "Sorting or moving a large scrap batch changed dispatch or retention\n";
			return false;
		}
	}
	if (!workshop.BalancedAt(8) || workshop.peakHolds != 9 || workshop.legitimateDestructions != 0 ||
		!std::all_of(references.begin(), references.end(), [](const Reference& reference) {
			return reference.BalancedAt(1) && reference.peakHolds == 2 && reference.legitimateDestructions == 0;
		})) {
		std::cerr << "Large scrap batch overflowed or failed to restore its reference counts\n";
		return false;
	}
	return true;
}

inline bool TestMixedOwners()
{
	Reference first{ 1 }, second{ 2 }, third{ 3 }, unowned{ 4 };
	Reference firstWorkshop{ 100 }, secondWorkshop{ 101 };
	{
		TestBatch batch;
		batch.Add(&first, &firstWorkshop);
		batch.Add(&second, &firstWorkshop);
		batch.Add(&third, &secondWorkshop);
		batch.Add(&unowned, nullptr);
		batch.Add(&firstWorkshop, &firstWorkshop);
		batch.Add(nullptr, nullptr);
		if (!firstWorkshop.BalancedAt(3) || firstWorkshop.peakHolds != 3 ||
			!secondWorkshop.BalancedAt(2) || secondWorkshop.peakHolds != 2) {
			std::cerr << "Mixed scrap owners were not retained once plus any selected-target hold\n";
			return false;
		}
		bool validOwners = true;
		unsigned attempts{};
		const auto count = DispatchLiveTargets(batch.Targets(),
			[](const Reference* reference) { return reference->deleted; },
			[&](Reference* reference, Reference* owner) {
				++attempts;
				auto* expectedOwner = reference == &unowned ? nullptr :
					reference == &third ? &secondWorkshop : &firstWorkshop;
				validOwners = validOwners && owner == expectedOwner;
				return reference != &unowned;
			});
		if (!validOwners || attempts != 5 || count != 4) {
			std::cerr << "Scrap dispatch changed mixed/null ownership or counted a failed dispatch\n";
			return false;
		}
	}
	for (const auto* reference : { &first, &second, &third, &unowned, &firstWorkshop, &secondWorkshop }) {
		if (!reference->BalancedAt(1) || reference->legitimateDestructions != 0) {
			std::cerr << "Mixed-owner scrap batch failed to release its own holds\n";
			return false;
		}
	}
	return true;
}

inline bool TestDeletedTargetsAndReleaseOrder()
{
	std::vector<unsigned> destructionOrder;
	destructionOrder.reserve(2);
	Reference endpoint{ 1 }, wire{ 2, 0 }, survivor{ 3 }, workshop{ 4, 0 }, alreadyDeleted{ 5 };
	alreadyDeleted.deleted = true;
	wire.destructionOrder = &destructionOrder;
	workshop.destructionOrder = &destructionOrder;
	RetainedReference engineWire{ &wire }, engineWorkshop{ &workshop };
	{
		TestBatch batch;
		batch.Add(&endpoint, &workshop);
		batch.Add(&wire, &workshop);
		batch.Add(&survivor, &workshop);
		batch.Add(&alreadyDeleted, &workshop);
		batch.Add(nullptr, nullptr);
		std::vector<unsigned> dispatched;
		bool validOwners = true;
		const auto count = DispatchLiveTargets(batch.Targets(),
			[](const Reference* reference) { return reference->deleted; },
			[&](Reference* reference, Reference* owner) {
				dispatched.push_back(reference->id);
				if (reference == &endpoint) {
					validOwners = validOwners && owner == &workshop;
					// Endpoint teardown deletes a later selected wire and drops
					// the engine's holds. The batch must retain both until cleanup.
					wire.deleted = true;
					workshop.deleted = true;
					engineWire.reset();
					engineWorkshop.reset();
				} else if (reference == &survivor) {
					validOwners = validOwners && owner == nullptr;
				}
				return true;
			});
		if (count != 2 || dispatched != std::vector<unsigned>{ 1, 3 } || !validOwners ||
			!wire.BalancedAt(1) || !workshop.BalancedAt(1) || !destructionOrder.empty()) {
			std::cerr << "Scrap dispatch lost lifetime or retried deleted targets/owners\n";
			return false;
		}
	}
	if (!wire.BalancedAt(0) || !workshop.BalancedAt(0) ||
		wire.legitimateDestructions != 1 || workshop.legitimateDestructions != 1 ||
		destructionOrder != std::vector<unsigned>{ 2, 4 } ||
		!endpoint.BalancedAt(1) || !survivor.BalancedAt(1) || !alreadyDeleted.BalancedAt(1)) {
		std::cerr << "Scrap batch leaked holds or released its workshop before selected targets\n";
		return false;
	}
	return true;
}

inline bool TestSelectedWorkshopReleaseOrder()
{
	std::vector<unsigned> destructionOrder;
	destructionOrder.reserve(2);
	Reference target{ 1, 0 }, workshop{ 2, 0 };
	target.destructionOrder = &destructionOrder;
	workshop.destructionOrder = &destructionOrder;
	{
		TestBatch batch;
		batch.Add(&target, &workshop);
		batch.Add(&workshop, &workshop);
		if (!target.BalancedAt(1) || !workshop.BalancedAt(2)) {
			std::cerr << "Selected workshop did not retain independent target and owner holds\n";
			return false;
		}
	}
	if (!target.BalancedAt(0) || !workshop.BalancedAt(0) ||
		target.legitimateDestructions != 1 || workshop.legitimateDestructions != 1 ||
		destructionOrder != std::vector<unsigned>{ 1, 2 }) {
		std::cerr << "Selected workshop was released before dependent target cleanup\n";
		return false;
	}
	return true;
}

inline bool TestRetainedScrapTargets()
{
	return TestLegacyOverflowModel() && TestLargeSharedWorkshop(1058) &&
		TestLargeSharedWorkshop(5000) && TestMixedOwners() &&
		TestDeletedTargetsAndReleaseOrder() && TestSelectedWorkshopReleaseOrder();
}

}
