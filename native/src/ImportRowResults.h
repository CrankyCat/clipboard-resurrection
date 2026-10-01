// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include "ImportJobState.h"
#include "WorkshopCallbackState.h"
#include <span>
#include <unordered_set>

namespace Clipboard::ImportRows
{
	enum Outcome : std::int32_t { Missing, Complete, Failed, Pending };
	inline constexpr std::size_t kPreparationHeader = 10;
	inline constexpr std::size_t kWorkshopHeader = 11;

	[[nodiscard]] inline Outcome Classify(const ImportJobs::Row& row) noexcept
	{
		switch (row.stage) {
		case ImportJobs::RowStage::Missing: return Missing;
		case ImportJobs::RowStage::Complete: return Complete;
		case ImportJobs::RowStage::Failed: return Failed;
		default: return Pending;
		}
	}
	[[nodiscard]] inline Outcome Classify(const WorkshopCallbacks::Row& row) noexcept
	{
		if (row.skipped) { return Missing; }
		if (row.failed) { return Failed; }
		return !row.inFlight && row.phase == WorkshopCallbacks::Phase::Complete ? Complete : Pending;
	}
	[[nodiscard]] inline bool CanContinue(const WorkshopCallbacks::State& state)
	{
		return state.done && !state.cancelled && !state.interrupted && !state.timedOut &&
			state.outstanding == 0 && state.Valid();
	}

	// Extended APIs retain their original aggregate fields, then add an explicit
	// safe-to-continue bit and one outcome for each physical input row. Legacy
	// APIs still return exactly nine/ten aggregate fields.
	[[nodiscard]] inline bool Valid(std::span<const std::int32_t> result, std::size_t count, bool workshop) noexcept
	{
		const auto header = workshop ? kWorkshopHeader : kPreparationHeader;
		if (count > ImportJobs::kMaximumRows || result.size() != header + count || result[header - 1] != 1 ||
			result[0] < 0 || result[0] > 1) { return false; }
		std::size_t missing{}, complete{}, failed{}, pending{};
		for (std::size_t i = header; i < result.size(); ++i) {
			switch (result[i]) {
			case Missing: ++missing; break;
			case Complete: ++complete; break;
			case Failed: ++failed; break;
			case Pending: ++pending; break;
			default: return false;
			}
		}
		for (std::size_t i = 1; i < header; ++i) { if (result[i] < 0) { return false; } }
		const auto countAt = [&](std::size_t i) { return static_cast<std::size_t>(result[i]); };
		if (workshop) {
			return pending == 0 && result[5] == 0 && countAt(6) == missing && countAt(7) == complete && countAt(4) == failed &&
				result[3] <= result[2] && result[2] <= result[1] &&
				(result[0] == 1) == (failed == 0) && (failed != 0 || result[3] == result[1]);
		}
		return countAt(1) == count - missing && countAt(2) == complete && countAt(6) == pending && countAt(7) == failed &&
			(result[0] == 1) == (failed == 0 && pending == 0);
	}

	// A duplicate row may reuse a newly placed reference from an earlier row.
	// Exclusion follows reference identity, not just the row containing a failure.
	[[nodiscard]] inline std::unordered_set<std::uint32_t> Excluded(
		std::span<const std::uint32_t> originals, std::span<const std::uint32_t> successful)
	{
		std::unordered_set<std::uint32_t> allowed;
		for (const auto id : successful) { if (id) { allowed.insert(id); } }
		std::unordered_set<std::uint32_t> excluded;
		for (const auto id : originals) { if (id && !allowed.contains(id)) { excluded.insert(id); } }
		return excluded;
	}
}
