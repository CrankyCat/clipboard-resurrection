// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ConduitPairTrace.h"
#include <stdexcept>

template<class Check> void RunConduitPairTraceChecks(Check&& check)
{
	using namespace Clipboard::ConduitConnections::Trace;
	const auto previousLogging = Clipboard::Logging::Enabled();
	Clipboard::Logging::SetEnabled(true);
	check(Key(0xFF001234, 0xFE002345) == Key(0xFE002345, 0xFF001234), "trace keys preserve reversed full IDs");
	check(Bits(false, false) == 4 && Bits(true, false) == 5 && Bits(false, true) == 6 && Bits(true, true) == 7,
		"trace retains both directional bits independently");
	check(!Incomplete(0) && Incomplete(4) && Incomplete(5) && Incomplete(6) && !Incomplete(7),
		"unavailable observation is not a missing edge");
	Session session(23);
	session.AddRow(20, 51);
	session.AddRow(10, 9000);
	session.AddRow(20, 99);
	Endpoint source{ 20, 101 }, target{ 10, 102 };
	const auto index = session.MatchPair(source, target, true, 4, { 1, 2, 3 }, 4);
	session.Observe(index, Point::AssemblyAdded, 7);
	check(session.pairs[index].low.ref == 10 && session.pairs[index].low.row == 9000 && session.pairs[index].high.row == 51,
		"physical rows survive normalization and duplicate reference mapping");
	check(session.pairs[index].assembly.source == 20 && session.pairs[index].assembly.pointIndex == 4 && session.events.empty(),
		"assembly query provenance retained without spending change budget on insertion");
	check(session.MatchPair(target, source, true, 9, {}, 7) == index && session.pairs.size() == 1 && session.pairs[index].assembly.pointIndex == 4,
		"duplicate pair discovery preserves first assembly provenance");
	session.Observe(index, Point::AssemblyEnd, 7);
	session.assemblyFinished = true;
	session.Observe(index, Point::PreRefresh, 5);
	check(session.events.size() == 1 && session.events[0].before.bits == 7 && session.events[0].after.bits == 5,
		"one-direction loss between assembly and refresh is retained");
	session.MatchPair(source, target, false, 6, { 4, 5, 6 }, 5);
	check(!session.NewToRefresh(session.pairs[index]), "previous assembly pair is not called new at refresh");
	const auto fresh = session.MatchPair({ 30, 103 }, source, false, 1, {}, 4);
	check(session.NewToRefresh(session.pairs[fresh]) && session.pairs[fresh].high.row == NoRow,
		"new refresh endpoint remains distinct from imported rows");
	session.assemblyDropped = 1;
	check(!session.NewToRefresh(session.pairs[fresh]), "truncated assembly cannot prove new refresh membership");
	session.assemblyDropped = 0;
	std::uint8_t graph = 7;
	session.slice = 15;
	session.AroundUpdate(10, Operation::Source, true, [&](const Pair&) { return graph; });
	graph = 4; // fake original engine update, exactly once
	session.AroundUpdate(10, Operation::Source, false, [&](const Pair&) { return graph; });
	const auto& loss = session.events.back();
	check(loss.before.point == Point::BeforeUpdate && loss.after.point == Point::AfterUpdate &&
		loss.before.operation == loss.after.operation && loss.after.operation == 1 && loss.after.actor == 10 &&
		loss.after.slice == 15 && loss.after.kind == Operation::Source && loss.after.sequence > loss.before.sequence,
		"change is bracketed around one specific source update in sequence");
	session.Observe(index, Point::Final, 7);
	check(Session::Interesting(session.pairs[index]) && session.pairs[index].refreshChanged,
		"transient loss remains interesting even after recovery");
	Session failure(24);
	failure.MatchPair(source, target, true, 0, {}, 7);
	int operations = 0;
	failure.AroundUpdate(10, Operation::Target, true, [](const Pair&) -> std::uint8_t { throw std::runtime_error("read"); });
	++operations;
	failure.AroundUpdate(10, Operation::Target, false, [](const Pair&) { return std::uint8_t{7}; });
	check(operations == 1 && failure.error && !failure.Active(), "diagnostic exception does not prevent original operation");
	const auto prior = session.sequence;
	session.Cancel();
	session.AroundUpdate(10, Operation::Target, true, [](const Pair&) { return std::uint8_t{4}; });
	session.Safely([&] { session.AddRow(99, 88); });
	check(session.sequence == prior && !session.rows.contains(99), "cancelled session stops numeric observations and writes");
	Session next(25);
	check(next.Active() && next.pairs.empty() && next.rows.empty() && next.token != session.token, "next import has no stale membership");
	for (std::uint32_t n = 0; n < PairLimit + 1; ++n) { next.MatchPair({ n + 1 }, { 100000 }, true, 0, {}, 7); }
	check(next.pairs.size() == PairLimit && next.pairsDropped == 1 && next.assemblyDropped == 1,
		"pair allocation and assembly incompleteness are explicitly bounded");
	std::size_t reads = 0;
	next.AroundUpdate(100000, Operation::Target, true, [&](const Pair&) { ++reads; return std::uint8_t{7}; });
	check(reads == IncidentLimit && next.incidentChecksDropped == PairLimit - IncidentLimit,
		"high-degree endpoint checks have explicit per-update bound");
	for (std::uint32_t n = 0; n < RowLimit + 1; ++n) { next.AddRow(n + 1, n); }
	check(next.rows.size() == RowLimit && next.rowsDropped == 1, "row map is bounded");
	for (std::size_t n = 0; n < EventLimit + 2; ++n) { next.Observe(0, Point::Final, (n % 2) ? 7 : 4); }
	check(next.events.size() == EventLimit && next.eventsDropped == 2, "change buffer reports truncation without losing final state");

	Session toggled(26);
	toggled.MatchPair(source, target, true, 0, {}, 7);
	toggled.assemblyFinished = true;
	const auto newPair = toggled.MatchPair({ 30, 103 }, source, false, 1, {}, 7);
	check(toggled.NewToRefresh(toggled.pairs[newPair]), "complete enabled trace may classify new refresh membership");
	std::size_t toggleReads{};
	int updates{};
	const auto performUpdate = [&] {
		toggled.AroundUpdate(20, Operation::Source, true, [&](const Pair&) { ++toggleReads; return std::uint8_t{7}; });
		++updates; // Original engine work is independent of optional collection.
		toggled.AroundUpdate(20, Operation::Source, false, [&](const Pair&) { ++toggleReads; return std::uint8_t{7}; });
	};
	performUpdate();
	const auto enabledReads = toggleReads;
	const auto enabledSequence = toggled.sequence;
	Clipboard::Logging::SetEnabled(false);
	performUpdate();
	check(enabledReads > 0 && toggleReads == enabledReads && toggled.sequence == enabledSequence && updates == 2,
		"logging Off stops diagnostic graph reads while original refresh still executes once");
	Clipboard::Logging::SetEnabled(true);
	performUpdate();
	check(toggleReads == enabledReads && toggled.sequence == enabledSequence && updates == 3 && toggled.loggingStopped &&
		!toggled.Active() && !toggled.NewToRefresh(toggled.pairs[newPair]),
		"logging On cannot resume a partial trace or claim complete cross-pass membership");
	Clipboard::Logging::SetEnabled(previousLogging);
}
