// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ImportRowResults.h"

template <class Check>
void CheckImportRowResults(Check&& check)
{
	using namespace Clipboard::ImportRows;
	std::vector<std::int32_t> preparation{ 0, 3, 1, 2, 1, 0, 1, 1, 100, 1, Missing, Complete, Failed, Pending };
	check(Valid(preparation, 4, false), "partial preparation retains a verified successful row and excludes failed/unready rows");
	for (std::size_t i : { 0u, 1u, 2u, 6u, 7u, 9u, 12u }) {
		auto bad = preparation; bad[i] = -1;
		check(!Valid(bad, 4, false), "negative/cancelled or malformed preparation outcome fails closed");
	}
	auto bad = preparation; bad[2] = 2;
	check(!Valid(bad, 4, false), "aggregate ready count must match physical row outcomes");
	bad = preparation; bad[9] = 0;
	check(!Valid(bad, 4, false), "unknown preparation completion never supplies a usable row map");
	check(!Valid(preparation, 3, false) && !Valid(preparation, 4, true), "wrong array size or stage cannot be consumed");

	Clipboard::WorkshopCallbacks::State state(3);
	check(state.SkipRow(0), "missing physical row is deliberately omitted");
	while (!state.done) {
		auto token = state.Reserve(0);
		if (!token) { break; }
		if (token->row == 1) { check(state.Reject(*token), "failed row rejects without inventing returns"); }
		else { check(state.Accept(*token) && state.Complete(*token), "successful row consumes actual per-phase returns"); }
	}
	check(CanContinue(state) && state.returned == 4 && state.planned == 8 && state.failed == 1,
		"drained failure permits only completed rows without rewriting failed planned calls");
	auto summary = state.Summary();
	std::vector<std::int32_t> workshop(summary.begin(), summary.end());
	workshop.push_back(CanContinue(state));
	for (const auto& row : state.rows) { workshop.push_back(Classify(row)); }
	check(Valid(workshop, 3, true), "validated mixed workshop result carries a safe continuation flag");
	bad = workshop; bad[5] = 1;
	check(!Valid(bad, 3, true), "outstanding callback prevents partial continuation");
	bad = workshop; bad.back() = Pending;
	check(!Valid(bad, 3, true), "unfinished workshop row cannot masquerade as drained failure");
	for (auto flag : { &state.cancelled, &state.interrupted, &state.timedOut }) {
		*flag = true; check(!CanContinue(state), "cancel/interruption/timeout always stops wiring and power"); *flag = false;
	}
	Clipboard::WorkshopCallbacks::State active(1);
	auto token = active.Reserve(0);
	check(token && active.Accept(*token) && !CanContinue(active), "accepted but unreturned callback cannot pass the barrier");
	check(active.Complete(*token, true) && CanContinue(active), "actual terminal cancellation excludes its row after drainage");
	const std::vector<std::uint32_t> originals{ 10, 20, 0, 20 }, success{ 10, 0, 0, 0 };
	const auto excluded = Excluded(originals, success);
	check(excluded.contains(20) && !excluded.contains(10) && !excluded.contains(0) && !excluded.contains(99),
		"failed new identity is excluded across duplicate aliases; unrelated existing wire endpoints stay eligible");
}
