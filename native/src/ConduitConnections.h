// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ConduitPairTrace.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

namespace RE
{
	class BGSKeyword;
	class TESObjectREFR;
}

namespace Clipboard::ConduitConnections
{
	class AssemblyPlan;
	[[nodiscard]] bool IsAssemblyPlanCancelled(const std::shared_ptr<AssemblyPlan>& plan) noexcept;
	enum class Progress
	{
		Complete,
		InProgress,
		Pending
	};

	// Separate factories select these diagnostic passes; existing callers retain
	// the combined path. No mode is read from untrusted serialized integers.
	enum class ExecutionMode { Combined, ConnectionsOnly, RefreshOnly };

	struct Counts
	{
		std::uint64_t references{};
		std::uint64_t candidates{};
		std::uint64_t points{};
		std::uint64_t matches{};
		std::uint64_t added{};
		std::uint64_t existing{};
		std::uint64_t pending{};  // Readiness encounters, not unique references.
		std::uint64_t failed{};
		std::uint64_t ineligible{};
		std::uint64_t missingParents{};
		std::uint64_t rejectedEndpoints{};
		std::uint64_t duplicatePairs{};
		std::uint64_t refreshed{};
		std::uint64_t noReference{};
		std::uint64_t noSnapPoint{};
		std::uint64_t nonReferenceHit{};
		std::uint64_t unexpectedQueryStatus{};

		// Skipped model points and rejected self/foreign/unowned endpoints are
		// diagnostic exclusions. Only a failed prerequisite/query/edge mutation
		// contributes to the aggregate failure result; pending rows are separate.
		[[nodiscard]] constexpr bool HasFailures() const noexcept { return failed != 0; }

		[[nodiscard]] constexpr bool MigrateVersion1Failures() noexcept
		{
			if (failed < missingParents) { return false; }
			failed -= missingParents;
			return true;
		}
	};

	// Shared execution policy, exercised with fake engine operations by host tests.
	// RefreshOnly must never create a newly discovered/missing edge. Combined
	// preserves the existing target refresh even when AddConnection fails.
	template <class HasEdge, class AddEdge, class RefreshTarget>
	void ProcessMatchedEdge(ExecutionMode mode, Counts& counts,
		HasEdge&& hasEdge, AddEdge&& addEdge, RefreshTarget&& refreshTarget)
	{
		if (hasEdge()) {
			++counts.existing;
		} else if (mode == ExecutionMode::RefreshOnly) {
			++counts.failed;
			return;
		} else {
			addEdge();
			if (hasEdge()) { ++counts.added; }
			else { ++counts.failed; }
		}
		if (mode != ExecutionMode::ConnectionsOnly) {
			refreshTarget();
			++counts.refreshed;
		}
	}

	// A batch belongs to one job, on the game/F4SE task thread. Pair memory is
	// only an optimization: existing direct workshop graph edges are checked before
	// engine mutation, including after a job resumes with a fresh batch.
	struct Batch
	{
		Counts counts;
		// The engine task owns the containers; cancellation/generation may change
		// elsewhere. Check it between engine operations, including graph reads.
		std::function<bool()> cancelled;
		[[nodiscard]] bool Cancelled() const noexcept
		{
			try { return IsAssemblyPlanCancelled(assemblyPlan) || (cancelled && cancelled()); }
			catch (...) { return true; }
		}
		std::unordered_set<std::uint64_t> pairs;
		// Empty for existing callers. Scoped imports rebuild this from retained,
		// remapped row references; never serialize these raw IDs.
		std::unordered_set<std::uint32_t> excludedReferences;
		std::shared_ptr<Trace::Session> trace; // Split diagnostic only; never saved.
		std::shared_ptr<AssemblyPlan> assemblyPlan; // Functional split membership; independent of trace.
	};

	// No retained engine pointers. The caller owns source/workshop handles and
	// must re-resolve them for every slice. Keep one cursor per source during a
	// live job. On save/load preserve only started/candidate for stable counts;
	// reset FormID/point index/complete and restart the graph-checked scan. The
	// next call binds a zero sourceFormID to the freshly resolved reference.
	struct ReferenceCursor
	{
		std::uint32_t sourceFormID{};
		std::size_t nextPoint{};
		bool started{};
		bool candidate{};
		bool complete{};
	};

	[[nodiscard]] constexpr std::uint64_t PairKey(std::uint32_t a_first, std::uint32_t a_second) noexcept
	{
		const auto low = a_first < a_second ? a_first : a_second;
		const auto high = a_first < a_second ? a_second : a_first;
		return (static_cast<std::uint64_t>(low) << 32) | high;
	}

	[[nodiscard]] constexpr bool IsSameWorkshopPair(
		std::uint32_t a_source, std::uint32_t a_target,
		std::uint32_t a_sourceWorkshop, std::uint32_t a_targetWorkshop) noexcept
	{
		return a_source != 0 && a_target != 0 && a_source != a_target &&
			a_sourceWorkshop != 0 && a_sourceWorkshop == a_targetWorkshop;
	}

	[[nodiscard]] constexpr bool IsDirectConnection(
		std::uint32_t a_expectedTarget, std::uint32_t a_connection, std::uint32_t a_connector) noexcept
	{
		return a_expectedTarget != 0 && a_connection == a_expectedTarget && a_connector == 0;
	}

	// Processes at most maxPointVisits CPA entries. Complete also covers refs
	// without eligible named-parent CPA points (see Counts). Pending means a
	// source/endpoint 3D or physics prerequisite is unavailable; the caller owns
	// the bounded retry interval and final timeout. No sleeping or worker calls.
	// expectedWorkshop may be null for the legacy per-source owning-workshop
	// behavior; an import job should supply its validated destination workshop.
	[[nodiscard]] Progress ProcessReference(
		RE::TESObjectREFR* a_source,
		RE::BGSKeyword* a_workshopItemKeyword,
		RE::TESObjectREFR* a_expectedWorkshop,
		Batch& a_batch,
		ReferenceCursor& a_cursor,
		std::size_t a_maxPointVisits = 16,
		ExecutionMode a_mode = ExecutionMode::Combined);

	[[nodiscard]] std::string Describe(const Counts& a_counts);
	// Bounded, read-only sweep. True means no diagnostic work remains. Failure
	// or cancellation abandons tracing without changing the import result.
	bool TraceBoundary(RE::TESObjectREFR* workshop, Batch& batch, Trace::Point point, std::size_t& cursor) noexcept;
	void LogPairTrace(Batch& batch, bool assembly, bool cancelled) noexcept;

	std::shared_ptr<AssemblyPlan> CreateAssemblyPlan(std::uint32_t tool, RE::TESObjectREFR* workshop,
		std::vector<std::uint32_t> rows);
	bool ClaimAssemblyPlan(const std::shared_ptr<AssemblyPlan>& plan, std::uint32_t tool,
		RE::TESObjectREFR* workshop, const std::vector<std::uint32_t>& rows);
	void CancelAssemblyPlan(const std::shared_ptr<AssemblyPlan>& plan) noexcept;
	void SealAssemblyPlan(const std::shared_ptr<AssemblyPlan>& plan, bool success) noexcept;
	// Functional before/after validation, not a diagnostic trace. Missing or
	// invalid intended edges remain failures even if geometry no longer finds them.
	bool ValidateAssemblyPlan(RE::TESObjectREFR* workshop, RE::BGSKeyword* keyword,
		Batch& batch, std::size_t& cursor, const char* phase);
	void LogAssemblyPlan(const Batch& batch, const char* phase);
}
