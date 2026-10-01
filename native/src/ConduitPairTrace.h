// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include "LoggingPolicy.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace Clipboard::ConduitConnections::Trace
{
	// Numeric, process-local observations only. Never serialize these IDs or
	// retain engine pointers. Containers belong to the game task thread; another
	// thread may only Cancel(). Diagnostic failure must not change engine work.
	inline constexpr std::size_t PairLimit = 8192, RowLimit = 16384, EventLimit = 4096;
	inline constexpr std::size_t IncidentLimit = 64, DetailLimit = 512, EventLogLimit = 512;
	inline constexpr std::uint32_t NoRow = std::numeric_limits<std::uint32_t>::max();
	// 0 = not observed/unavailable; bit 2 = read succeeded, bit 0 = low->high,
	// bit 1 = high->low. 4 is known absent; 7 is known complete.
	constexpr std::uint8_t Bits(bool lowToHigh, bool highToLow) noexcept
	{ return static_cast<std::uint8_t>(4 | (lowToHigh ? 1 : 0) | (highToLow ? 2 : 0)); }
	constexpr bool Incomplete(std::uint8_t bits) noexcept { return (bits & 4) && bits != 7; }
	constexpr std::uint64_t Key(std::uint32_t a, std::uint32_t b) noexcept
	{ return (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b); }
	enum class Point : std::uint8_t { AssemblyMatch, AssemblyAdded, AssemblyEnd, PreRefresh, RefreshMatch, BeforeUpdate, AfterUpdate, Final };
	enum class Operation : std::uint8_t { None, Target, Source };
	struct Endpoint
	{
		std::uint32_t ref{}, base{}, cell{}, type{}, row{ NoRow };
		std::array<float, 3> position{};
	};
	struct Observation
	{
		std::uint64_t sequence{}, elapsedMs{}, operation{};
		std::uint32_t slice{}, actor{};
		Point point{};
		Operation kind{};
		std::uint8_t bits{};
	};
	struct Match
	{
		Observation observation;
		std::uint32_t source{}, pointIndex{};
		std::array<float, 3> queryPosition{};
	};
	struct Pair
	{
		Endpoint low, high;
		bool assembled{}, refreshed{}, refreshChanged{};
		Match assembly, refresh;
		Observation added, assemblyEnd, preRefresh, final, last;
	};
	struct Change { std::size_t pair{}; Observation before, after; };

	struct Session
	{
		explicit Session(std::int32_t id) : token(id) {}
		void Cancel() noexcept { cancelled.store(true); }
		bool Active() noexcept
		{
			// Once collection observes logging Off, stop for this session. Turning
			// it back On cannot restore the missing cross-pass observations.
			if (!Logging::Enabled()) { loggingStopped = true; }
			return !cancelled.load() && !error && !loggingStopped;
		}
		template<class F> void Safely(F&& fn) noexcept
		{
			if (!Active()) { return; }
			try { fn(); } catch (...) { error = true; }
		}
		void AddRow(std::uint32_t ref, std::uint32_t row)
		{
			if (!ref || rows.contains(ref)) { return; }
			if (rows.size() == RowLimit) { ++rowsDropped; return; }
			rows.emplace(ref, row);
		}
		Observation Stamp(Point point, std::uint8_t bits)
		{
			return { ++sequence, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - start).count()), operation, slice, actor, point, kind, bits };
		}
		void Observe(std::size_t index, Point point, std::uint8_t bits)
		{
			auto& pair = pairs[index];
			const auto now = Stamp(point, bits);
			if (pair.last.sequence && pair.last.bits != bits) {
				if (point >= Point::RefreshMatch) { pair.refreshChanged = true; }
				// Expected assembly insertion is retained in each pair's baseline;
				// do not spend the change-event budget on thousands of those.
				if (point != Point::AssemblyAdded) {
					if (events.size() < EventLimit) { events.push_back({ index, pair.last, now }); }
					else { ++eventsDropped; }
				}
			}
			pair.last = now;
			switch (point) {
			case Point::AssemblyMatch: pair.assembly.observation = now; break;
			case Point::AssemblyAdded: pair.added = now; break;
			case Point::AssemblyEnd: pair.assemblyEnd = now; break;
			case Point::PreRefresh: pair.preRefresh = now; break;
			case Point::RefreshMatch: pair.refresh.observation = now; break;
			case Point::Final: pair.final = now; break;
			default: break;
			}
		}
		std::size_t MatchPair(Endpoint source, Endpoint target, bool assembly,
			std::uint32_t pointIndex, std::array<float, 3> query, std::uint8_t bits)
		{
			const auto key = Key(source.ref, target.ref);
			auto found = indices.find(key);
			if (found == indices.end()) {
				if (pairs.size() == PairLimit) { ++pairsDropped; if (assembly) { ++assemblyDropped; } return PairLimit; }
				for (auto* endpoint : { &source, &target }) {
					if (const auto row = rows.find(endpoint->ref); row != rows.end()) { endpoint->row = row->second; }
				}
				const auto index = pairs.size();
				Pair pair;
				pair.low = source.ref < target.ref ? source : target;
				pair.high = source.ref < target.ref ? target : source;
				pairs.push_back(pair);
				found = indices.emplace(key, index).first;
				incident[source.ref].push_back(index);
				incident[target.ref].push_back(index);
			}
			auto& pair = pairs[found->second];
			if (assembly ? pair.assembled : pair.refreshed) { return found->second; }
			(assembly ? pair.assembled : pair.refreshed) = true;
			auto& match = assembly ? pair.assembly : pair.refresh;
			match.source = source.ref;
			match.pointIndex = pointIndex;
			match.queryPosition = query;
			Observe(found->second, assembly ? Point::AssemblyMatch : Point::RefreshMatch, bits);
			return found->second;
		}
		bool NewToRefresh(const Pair& pair) const noexcept
		{ return pair.refreshed && !pair.assembled && assemblyFinished && assemblyDropped == 0 && !error && !loggingStopped && !cancelled.load(); }
		static bool Interesting(const Pair& pair) noexcept
		{
			return pair.refreshChanged || pair.assembled != pair.refreshed || Incomplete(pair.added.bits) || Incomplete(pair.assemblyEnd.bits) ||
				Incomplete(pair.preRefresh.bits) || Incomplete(pair.refresh.observation.bits) || Incomplete(pair.final.bits);
		}
		// Capture only pairs incident to the endpoint of an existing update. The
		// final sweep also catches nonlocal changes, but cannot name their writer.
		template<class Read> void AroundUpdate(std::uint32_t ref, Operation op, bool before, Read&& read) noexcept
		{
			Safely([&] {
				if (before) { ++operation; actor = ref; kind = op; }
				const auto found = incident.find(ref);
				if (found == incident.end()) { return; }
				const auto count = std::min(IncidentLimit, found->second.size());
				incidentChecksDropped += found->second.size() - count;
				for (std::size_t n = 0; n < count; ++n) {
					const auto index = found->second[n];
					Observe(index, before ? Point::BeforeUpdate : Point::AfterUpdate, read(pairs[index]));
				}
			});
		}
		const std::int32_t token;
		std::atomic_bool cancelled{};
		bool error{}, loggingStopped{}, assemblyFinished{}, preFinished{}, finalFinished{};
		std::uint32_t tool{}, workshop{}, assemblyStack{}, refreshStack{}, slice{}, actor{};
		std::uint64_t sequence{}, operation{}, pairsDropped{}, assemblyDropped{}, rowsDropped{}, eventsDropped{}, incidentChecksDropped{};
		Operation kind{};
		std::chrono::steady_clock::time_point start{ std::chrono::steady_clock::now() };
		std::unordered_map<std::uint32_t, std::uint32_t> rows;
		std::unordered_map<std::uint64_t, std::size_t> indices;
		std::unordered_map<std::uint32_t, std::vector<std::size_t>> incident;
		std::vector<Pair> pairs;
		std::vector<Change> events;
	};
}
