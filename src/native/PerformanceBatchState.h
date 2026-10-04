// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <cstdint>
#include <atomic>
#include <vector>

namespace Clipboard::PerformanceBatch
{
    inline constexpr std::uint32_t kMaximumRows = 262144;
    inline constexpr std::uint32_t kScrapsPerSlice = 2;
    inline constexpr std::uint32_t kAnimationRowsPerSlice = 64;
    inline constexpr std::uint64_t kBudgetMicroseconds = 2000;
    inline constexpr std::int32_t kScrapDelayMs = 100;

    // The delayed Papyrus worker coordinates; the engine task queue executes.
    // Only a completed task publishes mutable results. Save uses the last
    // consumed Progress, never memory that a queued task may still be changing.
    class SliceHandoff
    {
    public:
        bool TryQueue() noexcept
        {
            auto expected = Phase::Idle;
            return phase.compare_exchange_strong(expected, Phase::Pending, std::memory_order_acq_rel);
        }
        void Publish() noexcept { phase.store(Phase::Ready, std::memory_order_release); }
        bool Consume() noexcept
        {
            auto expected = Phase::Ready;
            return phase.compare_exchange_strong(expected, Phase::Idle, std::memory_order_acq_rel);
        }
    private:
        enum class Phase { Idle, Pending, Ready };
        std::atomic<Phase> phase{ Phase::Idle };
    };

    // One engine call cannot be preempted. Always allow progress, then yield
    // at either bound. Positive rescheduling prevents same-tick busy loops.
    constexpr bool SliceComplete(bool scrap, std::uint32_t visits, std::uint64_t microseconds)
    {
        return visits >= (scrap ? kScrapsPerSlice : kAnimationRowsPerSlice) ||
            (visits != 0 && microseconds >= kBudgetMicroseconds);
    }

    struct Progress
    {
        std::uint32_t total{}, cursor{}, dispatched{}, skipped{}, failed{};
        bool interrupted{};

        template<class Write> bool Save(Write&& write) const
        {
            return write(total) && write(cursor) && write(dispatched) && write(skipped) && write(failed);
        }
        template<class Read> bool Load(Read&& read, std::uint32_t inputRows)
        {
            Progress loaded;
            if (!read(loaded.total) || !read(loaded.cursor) || !read(loaded.dispatched) ||
                !read(loaded.skipped) || !read(loaded.failed) || loaded.total > inputRows ||
                inputRows > kMaximumRows || loaded.cursor > loaded.total ||
                static_cast<std::uint64_t>(loaded.dispatched) + loaded.skipped + loaded.failed > loaded.cursor) {
                return false;
            }
            // Destruction consumers have no serializable completion signal.
            // Preserve progress for reporting, but never replay after load.
            loaded.interrupted = true;
            *this = loaded;
            return true;
        }
    };

    template<class Visit, class Elapsed>
    void VisitSlice(Progress& progress, bool scrap, Visit&& visit, Elapsed&& elapsed)
    {
        std::uint32_t visits{};
        while (!progress.interrupted && progress.cursor < progress.total) {
            // Advance before dispatch; an exception or load must never replay
            // a destructive call whose effect is unknown.
            visit(progress.cursor++);
            if (SliceComplete(scrap, ++visits, elapsed())) { break; }
        }
    }

    // Compact physical-row/bitset pairs. Null rows are omitted by the caller;
    // unavailable loaded-state inspection must not disappear from the summary.
    inline void Classify(std::uint32_t row, std::int32_t kind,
        std::vector<std::int32_t>& pairs, std::uint32_t& unavailable)
    {
        if (kind < 0 || kind > 3) { ++unavailable; }
        else if (kind != 0) {
            pairs.push_back(static_cast<std::int32_t>(row));
            pairs.push_back(kind);
        }
    }
}
