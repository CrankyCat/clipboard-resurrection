// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>
#include "WorkshopCallbackDrain.h"

namespace Clipboard::WorkshopCallbacks
{
	inline constexpr std::uint32_t kMaximumRows = 262144;
	inline constexpr std::uint32_t kLegacyOutstanding = 4;
	inline constexpr std::uint32_t kDefaultOutstanding = 4;
	// Retain the tested eight-slot v4 layout when reading/resaving older jobs.
	// New throttled jobs use kDefaultOutstanding, not this compatibility limit.
	inline constexpr std::uint32_t kMaximumOutstanding = 8;
	// v5 widens the allowed throttled capacity; v1-v4 retain their exact readers.
	inline constexpr std::uint32_t kCurrentVersion = 5;
	// Five-minute observation ceiling requested for the drain diagnostic.
	inline constexpr std::uint64_t kCallTimeoutMs = 300000;
	inline constexpr std::uint64_t kProgressIntervalMs = 10000;

	enum class Phase : std::uint8_t { WorkshopPlaced, ObjectPlaced, WorkshopMoved, ObjectMoved, Complete };
	enum class Completion : std::uint8_t { Pending, Returned, Cancelled };
	// Baseline preserves the original phase waves for host comparisons. New
	// jobs prioritize returned rows while allowing every phase to use all four
	// direct-call slots when throttling is enabled. Off retains row ordering.
	enum class SchedulingPolicy : std::uint8_t { Baseline, ContinuationsFirst };
	struct AllowAdmission
	{
		bool operator()(std::uint32_t, Phase, std::uint64_t) const noexcept { return true; }
	};

	// A VM callback holds only this shared signal, never the owning job. Its
	// first terminal notification wins; duplicates and callbacks after timeout
	// cannot mutate a destroyed job or acknowledge a reused slot.
	struct CompletionSignal
	{
		std::atomic<Completion> outcome{ Completion::Pending };
		void Resolve(Completion value) noexcept
		{
			if (value == Completion::Pending) { return; }
			auto expected = Completion::Pending;
			(void)outcome.compare_exchange_strong(expected, value, std::memory_order_release, std::memory_order_relaxed);
		}
		[[nodiscard]] Completion Get() const noexcept { return outcome.load(std::memory_order_acquire); }
	};

	struct Token
	{
		std::uint32_t row{};
		Phase phase{ Phase::WorkshopPlaced };
		std::uint32_t slot{};
		std::uint64_t generation{};
		bool operator==(const Token&) const = default;
	};

	struct Row
	{
		Phase phase{ Phase::WorkshopPlaced };
		bool failed{};
		bool skipped{};
		bool inFlight{};
		std::uint8_t omittedMask{};
	};

	struct Slot
	{
		Token token;
		std::uint64_t startedMs{};
		bool active{};
		bool accepted{};
	};

	struct State
	{
		explicit State(std::uint32_t rowCount = 0, SchedulingPolicy policy = SchedulingPolicy::ContinuationsFirst,
			bool useThrottling = true, std::uint32_t throttledCapacity = kDefaultOutstanding,
			bool singleCallbackDiagnostic = false, std::int32_t callbackLimit = -1, std::int32_t callbackResumeAt = -1) :
			rows(CheckedRows(rowCount)), slots(StorageCapacity(rowCount, useThrottling, throttledCapacity, callbackLimit)),
			planned(rowCount * 4), done(rowCount == 0), schedulingPolicy(policy), throttling(useThrottling),
			singleCallback(singleCallbackDiagnostic && useThrottling && callbackLimit < 0)
		{
			drain = CallbackDrain(AdmissionLimit(), useThrottling && callbackLimit > 0, callbackResumeAt);
			for (std::uint32_t row = 0; row < rowCount; ++row) { ready.push_back(row); }
		}

		[[nodiscard]] std::uint32_t Capacity() const noexcept { return static_cast<std::uint32_t>(slots.size()); }
		// Single mode alone keeps its legacy slot width; an explicit limit uses
		// v5 capacity semantics. Loaded unfinished jobs never replay callbacks.
		[[nodiscard]] std::uint32_t AdmissionLimit() const noexcept { return singleCallback ? 1u : Capacity(); }
		[[nodiscard]] bool UsesThrottling() const noexcept { return throttling; }
		// Large all-row windows have a stable row-index slot. Avoid quadratic
		// free-slot scans while retaining old small-slot ordering and readers.
		[[nodiscard]] bool UsesRowSlots() const noexcept
		{
			return !throttling || (Capacity() > kMaximumOutstanding && slots.size() == rows.size());
		}
		[[nodiscard]] bool TimeoutCheckDue(std::uint64_t nowMs) const noexcept { return !done && nowMs >= nextTimeoutMs; }

		[[nodiscard]] bool SkipRow(std::uint32_t index)
		{
			if (index >= rows.size()) { return false; }
			auto& row = rows[index];
			if (done || row.inFlight || row.failed || row.skipped || row.phase != Phase::WorkshopPlaced) { return false; }
			row.skipped = true;
			++skippedRows;
			planned -= 4;
			FinishIfIdle();
			return true;
		}

		// Reserve before entering the engine: an empty method may complete
		// synchronously. Accept/reject the dispatch before consuming its signal.
		template<class Admission = AllowAdmission>
		[[nodiscard]] std::optional<Token> Reserve(std::uint64_t nowMs, Admission&& admit = {})
		{
			Tick(nowMs);
			if (done || outstanding >= AdmissionLimit()) { return std::nullopt; }
			// Tick still runs while closed: a drain must never extend an accepted
			// request's deadline or reserve/reorder work before its low threshold.
			if (throttling && !drain.Allows()) { return std::nullopt; }
			PruneReady();
			if (ready.empty() && continuations.empty()) { FinishIfIdle(); return std::nullopt; }
			// Returned-row continuations must not wait behind every untouched row.
			// Separate FIFOs avoid rescanning the row set. Untouched rows can use
			// every remaining slot, including when placed calls are already pending.
			auto& queue = continuations.empty() ? ready : continuations;
			const auto rowIndex = queue.front();
			// A diagnostic cooldown must not reserve a slot, start its watchdog,
			// change its generation or reorder rows while admission is held.
			if (throttling && !admit(rowIndex, rows[rowIndex].phase, nowMs)) { return std::nullopt; }
			// All-row windows use one slot per physical row, so callback ownership
			// never requires repeatedly scanning all earlier pending requests.
			std::uint32_t slotIndex = UsesRowSlots() ? rowIndex : 0u;
			if (!UsesRowSlots()) {
				while (slotIndex < slots.size() && slots[slotIndex].active) { ++slotIndex; }
			}
			if (slotIndex >= slots.size() || slots[slotIndex].active) { return std::nullopt; }
			queue.pop_front();
			auto& row = rows[rowIndex];
			Token token{ rowIndex, row.phase, slotIndex, nextGeneration++ };
			slots[slotIndex] = Slot{ token, nowMs, true, false };
			row.inFlight = true;
			++outstanding;
			peakOutstanding = std::max(peakOutstanding, outstanding);
			return token;
		}

		[[nodiscard]] bool Accept(const Token& token) noexcept
		{
			if (done || !Matches(token) || slots[token.slot].accepted) { return false; }
			slots[token.slot].accepted = true;
			if (throttling) {
				nextTimeoutMs = std::min(nextTimeoutMs, Deadline(slots[token.slot].startedMs));
			} else if (AcceptedPending() == 0) {
				// Off can queue more calls than the VM can start within the
				// timeout. Bound lack of real returns, not a queued call's age.
				nextTimeoutMs = Deadline(slots[token.slot].startedMs);
			}
			++dispatched;
			if (throttling) { drain.Accepted(AcceptedPending(), elapsedMs); }
			return true;
		}

		[[nodiscard]] bool Reject(const Token& token)
		{
			if (done || !Matches(token) || slots[token.slot].accepted) { return false; }
			++rejected;
			FailRow(token);
			return true;
		}

		[[nodiscard]] bool Complete(const Token& token, bool wasCancelled = false)
		{
			if (done || !Matches(token) || !slots[token.slot].accepted) { return false; }
			if (wasCancelled) { FailRow(token); return true; }
			++returned;
			if (!throttling) { nextTimeoutMs = Deadline(elapsedMs); }
			Advance(token);
			return true;
		}

		// Only a reserved, never-dispatched workshop phase may be omitted. This
		// is planned work removed by a verified condition, not a callback return.
		[[nodiscard]] bool Omit(const Token& token)
		{
			if (done || !Matches(token) || slots[token.slot].accepted ||
				(token.phase != Phase::WorkshopPlaced && token.phase != Phase::WorkshopMoved)) { return false; }
			rows[token.row].omittedMask |= static_cast<std::uint8_t>(1u << static_cast<unsigned>(token.phase));
			--planned;
			++omittedCalls;
			Advance(token);
			return true;
		}

		void Tick(std::uint64_t nowMs) noexcept
		{
			if (done) { return; }
			elapsedMs = std::max(elapsedMs, nowMs);
			// Accepted calls provide the earliest possible timeout. Polling and
			// reserving many Off-mode rows is constant-time before that deadline.
			if (nowMs < nextTimeoutMs) { return; }
			if (!throttling) {
				// Dispatch, omission and rejection do not reset this watchdog.
				// Only a successful callback return establishes script progress.
				if (AcceptedPending() != 0) { timedOut = done = true; }
				return;
			}
			nextTimeoutMs = std::numeric_limits<std::uint64_t>::max();
			for (const auto& slot : slots) {
				if (slot.active && slot.accepted && nowMs >= slot.startedMs && nowMs - slot.startedMs >= kCallTimeoutMs) {
					timedOut = done = true;
					return;
				}
				if (slot.active && slot.accepted) { nextTimeoutMs = std::min(nextTimeoutMs, Deadline(slot.startedMs)); }
			}
		}

		void Cancel() noexcept { cancelled = done = true; }

		[[nodiscard]] bool Matches(const Token& token) const noexcept
		{
			return token.slot < slots.size() && slots[token.slot].active && slots[token.slot].token == token;
		}

		[[nodiscard]] std::array<std::int32_t, 10> Summary() const noexcept
		{
			const auto status = cancelled || interrupted ? -1 : (!done || timedOut || failed || outstanding ? 0 : 1);
			const auto narrow = [](std::uint64_t value) { return static_cast<std::int32_t>(std::min<std::uint64_t>(value, INT32_MAX)); };
			return { status, narrow(planned), narrow(dispatched), narrow(returned), narrow(failed), narrow(outstanding),
				narrow(skippedRows), narrow(completedRows), narrow(elapsedMs), narrow(peakOutstanding) };
		}

		// Co-saves record counters/row phases/uncertain slots, never native callback
		// pointers. An unfinished restored job is interrupted, not replayed: the
		// VM may retain the callee's stack after dropping its unsaveable callback.
		template <class Write>
		[[nodiscard]] bool Save(Write&& write, std::uint32_t version = kCurrentVersion) const
		{
			if (version < 1 || version > kCurrentVersion || !Valid() || (version < 3 && !throttling) ||
				(version < 5 && throttling && !SupportedLegacyCapacity(Capacity())) ||
				(version < 4 && throttling && Capacity() != kLegacyOutstanding) ||
				(version == 1 && omittedCalls)) { return false; }
			// v1/v2 have four slots; v3 infers four for On or row count for Off.
			// v4 records legacy capacity explicitly; v5 permits wider On windows.
			if (version >= 3 && !write(throttling)) { return false; }
			if (version >= 4 && !write(Capacity())) { return false; }
			const auto count = static_cast<std::uint32_t>(rows.size());
			if (!write(count) || !write(planned) || !write(dispatched) || !write(returned) || !write(failed) ||
				!write(outstanding) || !write(skippedRows) || !write(completedRows) || !write(elapsedMs) ||
				!write(peakOutstanding) || !write(rejected) || !write(nextGeneration) ||
				!write(done) || !write(cancelled) || !write(interrupted) || !write(timedOut)) { return false; }
			for (const auto& row : rows) {
				const auto phase = static_cast<std::uint8_t>(row.phase);
				if (!write(phase) || !write(row.failed) || !write(row.skipped) || !write(row.inFlight)) { return false; }
			}
			for (const auto& slot : slots) {
				const auto phase = static_cast<std::uint8_t>(slot.token.phase);
				if (!write(slot.active) || !write(slot.accepted) || !write(slot.token.row) || !write(phase) ||
					!write(slot.token.slot) || !write(slot.token.generation) || !write(slot.startedMs)) { return false; }
			}
			// v2 appends masks after the intact v1 prefix. This keeps predecessor
			// payload interpretation explicit and makes omitted phases auditable.
			if (version >= 2) {
				for (const auto& row : rows) { if (!write(row.omittedMask)) { return false; } }
			}
			return true;
		}

		template <class Read>
		[[nodiscard]] bool Load(Read&& read, std::uint32_t version = kCurrentVersion)
		{
			if (version < 1 || version > kCurrentVersion) { return false; }
			State loaded;
			std::uint32_t count{}, capacity{};
			const auto readBool = [&](bool& value) {
				std::uint8_t raw{};
				if (!read(raw) || raw > 1) { return false; }
				value = raw != 0;
				return true;
			};
			if (version >= 3 && !readBool(loaded.throttling)) { return false; }
			if (version >= 4 && (!read(capacity) || capacity == 0 || capacity > kMaximumRows)) { return false; }
			if (!read(count) || count > kMaximumRows || !read(loaded.planned) || !read(loaded.dispatched) ||
				!read(loaded.returned) || !read(loaded.failed) || !read(loaded.outstanding) || !read(loaded.skippedRows) ||
				!read(loaded.completedRows) || !read(loaded.elapsedMs) || !read(loaded.peakOutstanding) ||
				!read(loaded.rejected) || !read(loaded.nextGeneration) || !readBool(loaded.done) ||
				!readBool(loaded.cancelled) || !readBool(loaded.interrupted) || !readBool(loaded.timedOut)) { return false; }
			if (version < 4) { capacity = loaded.throttling ? kLegacyOutstanding : std::max(1u, count); }
			if (loaded.throttling ?
				(version < 5 ? !SupportedLegacyCapacity(capacity) : !SupportedCapacity(capacity, count)) :
				capacity != std::max(1u, count)) { return false; }
			loaded.rows.resize(count);
			loaded.slots.resize(capacity);
			for (auto& row : loaded.rows) {
				std::uint8_t phase{};
				if (!read(phase) || phase > static_cast<std::uint8_t>(Phase::Complete) || !readBool(row.failed) ||
					!readBool(row.skipped) || !readBool(row.inFlight)) { return false; }
				row.phase = static_cast<Phase>(phase);
			}
			for (auto& slot : loaded.slots) {
				std::uint8_t phase{};
				if (!readBool(slot.active) || !readBool(slot.accepted) || !read(slot.token.row) || !read(phase) ||
					phase > static_cast<std::uint8_t>(Phase::Complete) || !read(slot.token.slot) ||
					!read(slot.token.generation) || !read(slot.startedMs)) { return false; }
				slot.token.phase = static_cast<Phase>(phase);
			}
			if (version >= 2) {
				for (auto& row : loaded.rows) {
					if (!read(row.omittedMask)) { return false; }
					loaded.omittedCalls += std::popcount(static_cast<unsigned>(row.omittedMask));
				}
			}
			if (!loaded.Valid()) { return false; }
			if (!loaded.done) { loaded.interrupted = loaded.done = true; }
			*this = std::move(loaded);
			return true;
		}

		[[nodiscard]] bool Valid() const
		{
			const auto validCapacity = throttling ? SupportedCapacity(Capacity(), static_cast<std::uint32_t>(rows.size())) : slots.size() == std::max<std::size_t>(1, rows.size());
			if (rows.size() > kMaximumRows || planned > rows.size() * 4 || dispatched > planned || returned > dispatched ||
				!validCapacity || failed > rows.size() || rejected > failed || outstanding > Capacity() || peakOutstanding < outstanding ||
				peakOutstanding > Capacity() || nextGeneration == 0 || nextGeneration > planned + omittedCalls + 1ULL ||
				((cancelled || interrupted || timedOut) && !done)) { return false; }
			std::uint32_t skipped{}, completed{}, failedRows{}, returnedCalls{}, activeRows{}, omitted{};
			for (const auto& row : rows) {
				const auto phase = static_cast<std::uint8_t>(row.phase);
				if (phase > 4 || (row.skipped && (phase != 0 || row.failed || row.inFlight)) ||
					(row.failed && (phase == 4 || row.inFlight)) || (phase == 4 && row.inFlight)) { return false; }
				if ((row.omittedMask & ~0x05u) != 0 || (row.omittedMask >> phase) != 0) { return false; }
				const auto rowOmitted = std::popcount(static_cast<unsigned>(row.omittedMask));
				skipped += row.skipped;
				completed += phase == 4;
				failedRows += row.failed;
				returnedCalls += phase - rowOmitted;
				omitted += rowOmitted;
				activeRows += row.inFlight;
			}
			std::uint32_t active{}, reserved{};
			std::vector<bool> seenRows(rows.size());
			std::vector<bool> seenGenerations(static_cast<std::size_t>(nextGeneration));
			for (std::uint32_t index = 0; index < slots.size(); ++index) {
				const auto& slot = slots[index];
				if (!slot.active) { continue; }
				const auto& token = slot.token;
				if (token.row >= rows.size() || token.slot != index || token.generation == 0 || token.generation >= nextGeneration ||
					token.phase != rows[token.row].phase || !rows[token.row].inFlight || slot.startedMs > elapsedMs ||
					(UsesRowSlots() && token.slot != token.row) || seenRows[token.row] || seenGenerations[token.generation]) { return false; }
				seenRows[token.row] = seenGenerations[token.generation] = true;
				++active;
				reserved += !slot.accepted;
			}
			if (skipped != skippedRows || completed != completedRows || failedRows != failed || returnedCalls != returned ||
				omitted != omittedCalls || planned + omitted != (rows.size() - skipped) * 4 || activeRows != outstanding || active != outstanding ||
				dispatched + rejected + reserved != returned + failed + outstanding) { return false; }
			return !done || cancelled || interrupted || timedOut || (outstanding == 0 && completed + failedRows + skipped == rows.size());
		}

		std::vector<Row> rows;
		std::vector<Slot> slots;
		std::uint32_t planned{};
		std::uint32_t dispatched{};
		std::uint32_t returned{};
		std::uint32_t failed{};
		std::uint32_t outstanding{};
		std::uint32_t skippedRows{};
		std::uint32_t completedRows{};
		std::uint64_t elapsedMs{};
		std::uint32_t peakOutstanding{};
		std::uint32_t rejected{};
		std::uint32_t omittedCalls{};
		std::uint64_t nextGeneration{ 1 };
		bool done{};
		bool cancelled{};
		bool interrupted{};
		bool timedOut{};
		// Not serialized. Load constructs a disabled policy and interrupts any
		// unfinished state; admission is never replayed from a restored payload.
		CallbackDrain drain;

	private:
		[[nodiscard]] static bool SupportedLegacyCapacity(std::uint32_t capacity) noexcept
		{
			return capacity == kLegacyOutstanding || capacity == kMaximumOutstanding;
		}
		[[nodiscard]] static bool SupportedCapacity(std::uint32_t capacity, std::uint32_t rowCount) noexcept
		{
			return SupportedLegacyCapacity(capacity) || (capacity >= 1 && capacity <= std::max(1u, rowCount) && capacity <= kMaximumRows);
		}
		[[nodiscard]] static std::uint32_t StorageCapacity(std::uint32_t rowCount, bool useThrottling,
			std::uint32_t legacyCapacity, std::int32_t limit)
		{
			if (limit < -1 || limit > static_cast<std::int32_t>(kMaximumRows)) { throw std::invalid_argument("Unsupported workshop callback limit"); }
			if (!useThrottling) { return std::max(1u, rowCount); }
			if (limit >= 0) {
				return limit == 0 ? std::max(1u, rowCount) : std::min(static_cast<std::uint32_t>(limit), std::max(1u, rowCount));
			}
			if (!SupportedLegacyCapacity(legacyCapacity)) { throw std::invalid_argument("Unsupported workshop callback capacity"); }
			return legacyCapacity;
		}
		[[nodiscard]] std::uint32_t AcceptedPending() const noexcept
		{
			return dispatched - returned - (failed - rejected);
		}
		static std::uint32_t CheckedRows(std::uint32_t count)
		{
			if (count > kMaximumRows) { throw std::length_error("Workshop callback row limit exceeded"); }
			return count;
		}
		static std::uint64_t Deadline(std::uint64_t started) noexcept
		{
			const auto maximum = std::numeric_limits<std::uint64_t>::max();
			return started > maximum - kCallTimeoutMs ? maximum : started + kCallTimeoutMs;
		}
		void Advance(const Token& token)
		{
			auto& row = rows[token.row];
			row.inFlight = false;
			row.phase = static_cast<Phase>(static_cast<std::uint8_t>(row.phase) + 1);
			slots[token.slot].active = false;
			--outstanding;
			if (throttling) { drain.Resolved(AcceptedPending(), elapsedMs); }
			if (row.phase == Phase::Complete) { ++completedRows; }
			else if (schedulingPolicy == SchedulingPolicy::Baseline) { ready.push_back(token.row); }
			else { continuations.push_back(token.row); }
			FinishIfIdle();
		}
		void PruneReady()
		{
			const auto prune = [&](auto& queue) {
				while (!queue.empty()) {
					const auto& row = rows[queue.front()];
					if (!row.skipped && !row.failed && !row.inFlight && row.phase != Phase::Complete) { break; }
					queue.pop_front();
				}
			};
			prune(ready);
			prune(continuations);
		}
		void FinishIfIdle()
		{
			PruneReady();
			if (ready.empty() && continuations.empty() && outstanding == 0) { done = true; }
		}
		void FailRow(const Token& token)
		{
			auto& row = rows[token.row];
			row.failed = true;
			row.inFlight = false;
			slots[token.slot].active = false;
			--outstanding;
			++failed;
			if (throttling) { drain.Resolved(AcceptedPending(), elapsedMs); }
			FinishIfIdle();
		}
		std::deque<std::uint32_t> ready;
		std::deque<std::uint32_t> continuations;
		// Neither policy nor ready queues belong to the v1 payload: unfinished
		// loads always interrupt without replay. Validation intentionally accepts
		// predecessor saves with four uncertain WorkshopPlaced calls.
		SchedulingPolicy schedulingPolicy{ SchedulingPolicy::ContinuationsFirst };
		bool throttling{ true };
		// Transient opt-in policy. Load builds a normal State and then marks any
		// unfinished work interrupted, so the diagnostic cannot resume on load.
		bool singleCallback{};
		std::uint64_t nextTimeoutMs{ std::numeric_limits<std::uint64_t>::max() };
	};
}
