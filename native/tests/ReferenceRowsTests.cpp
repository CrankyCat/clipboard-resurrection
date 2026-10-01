// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#include "FirstReferenceRows.h"
#include "LegacyVMArray.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <vector>

namespace
{
	struct Reference { std::uint32_t formID; };
	struct Work { std::uint64_t copiedRows{}, readRows{}, queries{}; };

	// Deliberately retain the historical owning argument and first-match loop
	// for a comparison against the production index, including VMArray copies.
	int HistoricalFind(VMArray<Reference*> objects, std::uint64_t id, Work& work)
	{
		work.copiedRows += objects.Length();
		++work.queries;
		for (std::uint32_t row = 0; row < objects.Length(); ++row) {
			Reference* object = nullptr;
			objects.Get(&object, row);
			++work.readRows;
			if (object && object->formID == id) { return static_cast<int>(row); }
		}
		return -1;
	}

	auto Index(const VMArray<Reference*>& objects, Work& work)
	{
		return Clipboard::Selection::BuildFirstReferenceRows(objects.Length(),
			[&](std::uint32_t row) -> std::optional<std::uint32_t> {
				Reference* object = nullptr;
				objects.Get(&object, row);
				++work.readRows;
				return object ? std::optional<std::uint32_t>{ object->formID } : std::nullopt;
			});
	}
}

int main()
{
	int checks{}, failures{};
	const auto check = [&](bool success, const char* message) {
		++checks;
		if (!success) { ++failures; std::cerr << message << '\n'; }
	};
	using Clipboard::Selection::FirstReferenceRows;
	Reference zero{0}, first{0x1234}, duplicate{0x1234}, light{0xFE001800}, created{0xFF002000};
	VMArray<Reference*> objects{ nullptr, &first, nullptr, &light, &duplicate, &zero, &created };
	Work fixtureWork;
	const auto rows = Index(objects, fixtureWork);
	check(fixtureWork.readRows == 7 && fixtureWork.copiedRows == 0, "one read per physical row, no owning array copy");
	check(rows.Find(first.formID) == 1, "duplicate identity preserves the first physical row");
	check(rows.Find(light.formID) == 3 && rows.Find(created.formID) == 6, "light and created reference identities remain full width");
	check(rows.Find(0) == 5, "a real zero identity is distinct from a None row");
	check(rows.Find(0x5678) == -1, "missing reference remains absent");
	check(rows.Find(0x100001234ull) == -1, "wide query cannot alias a 32-bit reference identity");
	FirstReferenceRows empty;
	check(empty.Find(0) == -1 && empty.Find(first.formID) == -1, "empty index has no manufactured row zero");
	VMArray<Reference*> nulls{ nullptr, nullptr, nullptr };
	const auto nullRows = Index(nulls, fixtureWork);
	check(nullRows.Find(0) == -1 && nullRows.Find(first.formID) == -1, "all None rows remain absent");

	// Use endpoint pairs that exercise holes, duplicate first matches, external
	// endpoints and repeated wires. The index must not reorder/deduplicate rows.
	constexpr std::array<std::array<std::uint32_t, 2>, 5> wires{{
		{0x1234, 0xFE001800}, {0xFF002000, 0x1234}, {0x1234, 0x5678},
		{0x1234, 0xFE001800}, {0, 0xFF002000}
	}};
	Work legacyWireWork;
	std::ostringstream before, after;
	for (std::uint32_t row = 0; row < wires.size(); ++row) {
		before << row << '=' << HistoricalFind(objects, wires[row][0], legacyWireWork) << '|'
			<< HistoricalFind(objects, wires[row][1], legacyWireWork) << '\n';
		after << row << '=' << rows.Find(wires[row][0]) << '|' << rows.Find(wires[row][1]) << '\n';
	}
	check(before.str() == after.str(), "wire endpoint rows match the historical physical-row lookup");
	check(after.str() == "0=1|3\n1=6|1\n2=1|-1\n3=1|3\n4=5|6\n", "wire order and duplicate wire rows remain unchanged");

	Reference later{0x5566};
	objects.Set(&objects[4], 1);
	objects.Set(&objects[5], 3);
	objects.push_back(&later);
	const auto nextRows = Index(objects, fixtureWork);
	check(nextRows.Find(light.formID) == -1 && nextRows.Find(later.formID) == 7,
		"a subsequent operation rebuilds the index from current references");
	check(rows.Find(light.formID) == 3 && rows.Find(later.formID) == -1,
		"operation snapshot is independent of later array mutation");

	// Broad selection source order is workshop children, tool cell, then the
	// represented workshop cells. Reject before inserting so rejected candidates
	// cannot suppress a later admissible row with the same identity.
	struct Candidate { Reference* ref; bool admitted; };
	const std::array<std::vector<Candidate>, 3> sources{{
		{{&first, true}, {nullptr, true}, {&light, false}, {&first, true}},
		{{&created, true}, {&light, true}, {&first, true}},
		{{&light, true}, {&later, false}, {&later, true}}
	}};
	VMArray<Reference*> legacyPool, indexedPool;
	FirstReferenceRows poolRows;
	Work legacyPoolWork;
	std::uint32_t oldAdmissions{}, newAdmissions{};
	for (const auto& source : sources) {
		for (const auto candidate : source) {
			++oldAdmissions;
			if (candidate.ref && candidate.admitted && HistoricalFind(legacyPool, candidate.ref->formID, legacyPoolWork) < 0) {
				legacyPool.push_back(candidate.ref);
			}
			++newAdmissions;
			if (candidate.ref && candidate.admitted && poolRows.Insert(candidate.ref->formID, indexedPool.Length())) {
				indexedPool.push_back(candidate.ref);
			}
		}
	}
	check(oldAdmissions == newAdmissions && oldAdmissions == 10, "per-candidate admission checks are preserved across sources");
	check(legacyPool.Length() == 4 && indexedPool.Length() == legacyPool.Length(), "source dedup preserves accepted count");
	for (std::uint32_t row = 0; row < legacyPool.Length(); ++row) {
		check(legacyPool[row] == indexedPool[row] && poolRows.Find(legacyPool[row]->formID) == static_cast<int>(row),
			"source dedup preserves first accepted order and row numbers");
	}
	check(!poolRows.Insert(first.formID, 99) && poolRows.Find(first.formID) == 0,
		"incremental duplicates cannot replace the first accepted row");

	// Deterministic mixed inputs compare every query to the original search.
	std::array<Reference, 31> references{};
	for (std::uint32_t i = 0; i < references.size(); ++i) { references[i].formID = i + 100; }
	std::uint32_t random = 17;
	for (std::uint32_t run = 0; run < 32; ++run) {
		VMArray<Reference*> mixture;
		for (std::uint32_t row = 0; row < 129; ++row) {
			random = random * 1664525u + 1013904223u;
			mixture.push_back(random % 7 == 0 ? nullptr : &references[random % references.size()]);
		}
		const auto mixedRows = Index(mixture, fixtureWork);
		for (std::uint32_t id = 99; id <= 131; ++id) {
			check(mixedRows.Find(id) == HistoricalFind(mixture, id, fixtureWork),
				"mixed 129-row query agrees with original null and duplicate semantics");
		}
	}

	// Work measurement only: 10,000 selected objects, 10,000 wires. Each wire
	// performs the discovery membership query and then two export row queries.
	// Count explicit array copies, row reads, and indexed queries, not elapsed
	// time or hash comparisons; these are not an in-game speedup benchmark.
	constexpr std::uint32_t count = 10000;
	std::vector<Reference> largeReferences(count);
	VMArray<Reference*> largeSelection;
	for (std::uint32_t row = 0; row < count; ++row) {
		largeReferences[row].formID = row + 1;
		largeSelection.push_back(&largeReferences[row]);
	}
	Work historical, optimized;
	const auto largeRows = Index(largeSelection, optimized);
	bool sameEndpoints = true;
	for (std::uint32_t wire = 0; wire < count; ++wire) {
		const std::uint32_t a = wire + 1, b = (wire + 1) % count + 1;
		for (const auto id : {b, a, b}) {
			const auto original = HistoricalFind(largeSelection, id, historical);
			++optimized.queries;
			sameEndpoints &= original == largeRows.Find(id);
		}
	}
	check(sameEndpoints, "10000-object wire discovery/export endpoint results match");
	check(historical.copiedRows == 300000000 && historical.readRows == 150015000 && historical.queries == 30000,
		"baseline wire workload is deterministic");
	check(optimized.copiedRows == 0 && optimized.readRows == 10000 && optimized.queries == 30000,
		"wire workload uses one index build and one map query per former search");

	VMArray<Reference*> largeLegacyPool, largeIndexedPool;
	FirstReferenceRows largePoolRows(count);
	Work oldPool;
	std::uint64_t insertAttempts{};
	for (const auto* source : {&largeSelection, &largeSelection}) {
		for (auto* reference : *source) {
			if (HistoricalFind(largeLegacyPool, reference->formID, oldPool) < 0) { largeLegacyPool.push_back(reference); }
			++insertAttempts;
			if (largePoolRows.Insert(reference->formID, largeIndexedPool.Length())) { largeIndexedPool.push_back(reference); }
		}
	}
	bool samePool = largeLegacyPool.Length() == largeIndexedPool.Length();
	for (std::uint32_t row = 0; row < count; ++row) { samePool &= largeLegacyPool[row] == largeIndexedPool[row]; }
	check(samePool && largeIndexedPool.Length() == count, "two 10000-object sources preserve pool order and unique count");
	check(oldPool.copiedRows == 149995000 && oldPool.readRows == 100000000 && insertAttempts == 20000,
		"pool work replaces repeated owning scans with one insertion attempt per admitted row");

	std::cout << checks << " reference-row checks, " << failures << " failures; "
		<< "wire workload: copied pointer slots " << historical.copiedRows << " -> " << optimized.copiedRows
		<< ", selection row reads " << historical.readRows << " -> " << optimized.readRows
		<< ", indexed queries " << optimized.queries
		<< "; pool workload: copied pointer slots " << oldPool.copiedRows
		<< " -> 0, comparison row reads " << oldPool.readRows << " -> 0, insertion attempts " << insertAttempts << '\n';
	return failures ? 1 : 0;
}
