// SPDX-License-Identifier: GPL-3.0-or-later
#include "ConduitConnections.h"
#include "ConduitPairTraceTests.h"
#include "ConduitAssemblyPlanTests.h"
#include "MotionTypeABI.h"

#include <array>
#include <cstring>
#include <iostream>
#include <set>

int main()
{
	using namespace Clipboard::ConduitConnections;
	int failures = 0;
	int checks = 0;
	const auto check = [&](bool a_passed, const char* a_description) {
		++checks;
		if (!a_passed) {
			++failures;
			std::cerr << a_description << '\n';
		}
	};
	RunConduitPairTraceChecks(check);
	RunConduitAssemblyPlanChecks(check);
	// Real runtime policies: reject self, missing owner and cross-workshop
	// matches even if the engine reports a physically touching snap point.
	check(IsSameWorkshopPair(0xFF001001, 0xFEABC123, 0x1234, 0x1234), "valid dynamic/ESL pair");
	check(!IsSameWorkshopPair(0xFF001001, 0xFF001001, 0x1234, 0x1234), "self-pair rejected");
	check(!IsSameWorkshopPair(0, 0xFF001001, 0x1234, 0x1234), "missing source rejected");
	check(!IsSameWorkshopPair(0xFF001001, 0, 0x1234, 0x1234), "missing target rejected");
	check(!IsSameWorkshopPair(1, 2, 0, 0), "missing owners rejected");
	check(!IsSameWorkshopPair(1, 2, 0x1234, 0x1235), "different workshop rejected");
	check(!IsSameWorkshopPair(1, 2, 0x1234, 0), "unowned target rejected");

	// A connector FormID denotes a wire. Its presence must not be reported as
	// a direct snapped edge, and a neighboring edge must not hide a missing one.
	check(IsDirectConnection(0xFFABCDEF, 0xFFABCDEF, 0), "direct snapped edge accepted");
	check(!IsDirectConnection(0xFFABCDEF, 0xFFABCDEF, 0xFF654321), "wire edge remains distinct");
	check(!IsDirectConnection(0xFFABCDEF, 0xFFABCDEE, 0), "different neighbor rejected");
	check(!IsDirectConnection(0, 0, 0), "null edge rejected");

	// Full 32-bit FormIDs survive pair normalization: reverse traversal,
	// duplicate CPA points and a fresh source cursor all use the same key.
	constexpr std::array ids{ 1u, 0xFE000001u, 0xFF000001u, 0xFFFFFFFFu };
	std::set<std::uint64_t> pairs;
	for (std::size_t first = 0; first < ids.size(); ++first) {
		for (std::size_t second = first + 1; second < ids.size(); ++second) {
			const auto key = PairKey(ids[first], ids[second]);
			check(key == PairKey(ids[second], ids[first]), "pair direction independence");
			check(pairs.insert(key).second, "distinct full-width pair keys");
		}
	}
	Batch batch;
	batch.pairs.insert(PairKey(1, 2));
	check(!batch.pairs.insert(PairKey(2, 1)).second, "duplicate pair suppressed within batch");
	ReferenceCursor cursor;
	check(!cursor.started && !cursor.complete && cursor.nextPoint == 0, "fresh/resumed scan starts at point zero");

	// Replay the Large Import 02 diagnostics: no generator was imported, 537
	// edges were added, and all reported failures were skipped model parents.
	// Model/endpoint exclusions must not turn that result into a power failure.
	Counts recorded;
	recorded.references = 1194;
	recorded.candidates = 1142;
	recorded.added = 537;
	recorded.missingParents = 4284;
	recorded.rejectedEndpoints = 1267;
	recorded.noReference = 1674;
	recorded.noSnapPoint = 1298;
	check(!recorded.HasFailures(), "large import exclusions are not failed connections");
	recorded.failed = 1;
	check(recorded.HasFailures(), "real connection failure remains visible without a generator");
	recorded.failed = 4284;
	check(recorded.MigrateVersion1Failures() && !recorded.HasFailures(), "old saved diagnostic totals migrate without a false warning");
	recorded.failed = 4285;
	check(recorded.MigrateVersion1Failures() && recorded.failed == 1 && recorded.HasFailures(), "migration retains a real failure alongside skipped parents");
	recorded.failed = 4283;
	check(!recorded.MigrateVersion1Failures() && recorded.failed == 4283, "inconsistent old totals are rejected without unsigned underflow");
	check(!Counts{}.HasFailures(), "empty connection job is successful no-work");

	// Execute the production edge policy against a fake engine graph. Split
	// assembly must not refresh; refresh must not create missing/changed edges.
	bool edge = false;
	bool addSucceeds = true;
	std::string operations;
	const auto hasEdge = [&] { return edge; };
	const auto addEdge = [&] { operations += 'A'; edge = addSucceeds; };
	const auto refresh = [&] { operations += 'R'; };
	Counts connections;
	ProcessMatchedEdge(ExecutionMode::ConnectionsOnly, connections, hasEdge, addEdge, refresh);
	check(edge && operations == "A" && connections.added == 1 && connections.refreshed == 0,
		"connection-only assembly adds an edge without refreshing it");
	Counts refreshes;
	ProcessMatchedEdge(ExecutionMode::RefreshOnly, refreshes, hasEdge, addEdge, refresh);
	check(operations == "AR" && refreshes.existing == 1 && refreshes.added == 0 && refreshes.refreshed == 1,
		"refresh-only pass uses the assembled graph without adding edges");
	edge = false;
	operations.clear();
	ProcessMatchedEdge(ExecutionMode::RefreshOnly, refreshes, hasEdge, addEdge, refresh);
	check(operations.empty() && !edge && refreshes.failed == 1 && refreshes.refreshed == 1,
		"missing edge during refresh fails without silent creation or target refresh");
	Counts combined;
	ProcessMatchedEdge(ExecutionMode::Combined, combined, hasEdge, addEdge, refresh);
	check(operations == "AR" && combined.added == 1 && combined.refreshed == 1,
		"normal combined path keeps add then refresh order");
	operations.clear();
	ProcessMatchedEdge(ExecutionMode::Combined, combined, hasEdge, addEdge, refresh);
	check(operations == "R" && combined.existing == 1 && combined.added == 1,
		"normal existing edge is refreshed without duplication");
	edge = false;
	addSucceeds = false;
	operations.clear();
	ProcessMatchedEdge(ExecutionMode::ConnectionsOnly, connections, hasEdge, addEdge, refresh);
	check(operations == "A" && connections.failed == 1 && connections.refreshed == 0,
		"failed split assembly remains unrefreshed and reports failure");
	operations.clear();
	ProcessMatchedEdge(ExecutionMode::Combined, combined, hasEdge, addEdge, refresh);
	check(operations == "AR" && combined.failed == 1,
		"normal failure path preserves its original refresh behavior");

	// The engine execution body consumes this storage through a Windows x64
	// ABI. Test the bytes received by that body, including activation/defaults.
	Clipboard::EngineAPI::MotionTypeABI::CallData motion;
	check(motion.motionType == 2 && motion.allowActivate, "keyframed motion preserves activation");
	check(motion.referenceHandle == 0 && motion.stackID == 0 && motion.vm == nullptr, "motion defaults do not retain engine state");
	check(motion.unusedHeader == std::array<std::byte, 0x0C>{}, "unused functor header stays inert");
	motion.stackID = 0x13572468;
	motion.referenceHandle = 0x87654321;
	std::uint32_t storedStack = 0;
	std::uint32_t storedHandle = 0;
	std::int32_t storedMotion = 0;
	const auto* bytes = reinterpret_cast<const std::byte*>(&motion);
	std::memcpy(&storedStack, bytes + 0x0C, sizeof(storedStack));
	std::memcpy(&storedHandle, bytes + 0x10, sizeof(storedHandle));
	std::memcpy(&storedMotion, bytes + 0x14, sizeof(storedMotion));
	check(storedStack == motion.stackID && storedHandle == motion.referenceHandle && storedMotion == 2,
		"motion engine-read offsets preserve distinct values");
	check(bytes[0x20] == std::byte{ 1 }, "allowActivate occupies reviewed engine byte");
	std::cout << checks << " import policy/ABI checks; " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
