// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ConduitAssemblyPlan.h"
#include <set>

template <class Check>
void RunConduitAssemblyPlanChecks(Check&& check)
{
	using Clipboard::ConduitConnections::AssemblyRecords;
	AssemblyRecords plan;
	check(!plan.Claim(), "refresh refuses an unfinished assembly");
	check(plan.Candidate(10) && plan.Candidate(20) && plan.Candidate(30), "assembly records source candidates");
	check(plan.Add(10, 20, 3, { 1, 2, 3 }), "assembly records a complete intended edge");
	check(plan.Add(20, 10, 7, { 4, 5, 6 }) && plan.pairs.size() == 1,
		"reverse discovery does not duplicate a refresh or replace its original source");
	check(plan.ForSource(10)->size() == 1 && plan.ForSource(20)->empty() && plan.ForSource(30)->empty(),
		"target-only and no-edge candidates still retain their source-refresh eligibility");
	check(plan.ForSource(40) == nullptr, "non-candidates do not gain source refresh");
	check(plan.pairs[0].source == 10 && plan.pairs[0].target == 20 && plan.pairs[0].point == 3,
		"first accepted direction and point survive reverse discovery");
	plan.Seal(true);
	check(plan.Ready() && plan.Claim() && !plan.Claim(), "only one refresh can claim a complete assembly");
	check(!plan.Add(10, 30, 8, {}) && plan.pairs.size() == 1, "sealed membership cannot accept a new neighbor");

	// Replay the measured alternative-neighbor failure: the assembly pair is
	// intact, but an independent geometry scan returns another target. Refresh
	// must keep its intended endpoint, and final validation must still inspect
	// an intended pair even if geometry would no longer rediscover it.
	AssemblyRecords doors;
	constexpr auto source = 0xFF3C3158u, intended = 0xFF3C3040u, alternative = 0xFF3C2B5Au;
	doors.Candidate(source);
	doors.Add(source, intended, 0, { 416.0296f, 2216.0283f, -232.00005f });
	doors.Seal(true);
	check(doors.Claim(), "recorded door fixture ready for refresh");
	std::vector<std::uint32_t> refreshed;
	for (const auto index : *doors.ForSource(source)) { refreshed.push_back(doors.pairs[index].target); }
	check(refreshed == std::vector<std::uint32_t>{ intended } && !doors.ForSource(alternative),
		"changed geometry cannot substitute the wall for the assembled neighbor");
	check(doors.MarkFailed(0) && !doors.MarkFailed(0), "a genuine missing intended edge is counted once across checks");
	check(doors.pairs[0].validationFailed, "later checks retain evidence of a genuine earlier connection loss");

	AssemblyRecords failed;
	failed.Seal(false);
	check(!failed.Ready() && !failed.Claim(), "incomplete assembly cannot authorize refresh");
	AssemblyRecords empty;
	empty.Seal(true);
	check(empty.Ready() && empty.Claim() && empty.pairs.empty(), "complete zero-pair assembly is valid");
	AssemblyRecords capped(1);
	capped.Candidate(1);
	check(capped.Add(1, 2, 0, {}) && !capped.Add(1, 3, 1, {}), "membership capacity overflow is explicit");
	capped.Seal(true);
	check(!capped.Ready() && !capped.Claim(), "truncated membership cannot certify a refresh");
	AssemblyRecords invalid;
	invalid.Candidate(1);
	check(!invalid.Add(1, 1, 0, {}), "self connection cannot enter intended pairs");
	invalid.Seal(true);
	check(!invalid.Claim(), "invalid record cannot be hidden by successful seal");

	// Larger than the numeric trace's 8,192-pair cap: operational correctness
	// must not depend on logging being enabled or a diagnostic buffer's capacity.
	AssemblyRecords large;
	large.Candidate(1);
	for (std::uint32_t id = 2; id <= 9002; ++id) { large.Add(1, id, id, {}); }
	large.Seal(true);
	check(large.Claim() && large.pairs.size() == 9001 && large.ForSource(1)->size() == 9001,
		"functional membership remains complete beyond diagnostic trace capacity");
}
