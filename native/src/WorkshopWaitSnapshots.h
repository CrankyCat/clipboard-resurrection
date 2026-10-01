// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include "WorkshopCallbackState.h"

namespace Clipboard::WorkshopCallbacks
{
    // Observe the single-call baseline or the explicit window experiment.
    // Ordinary admission, Off and the completed spacing comparison stay unchanged.
    [[nodiscard]] constexpr bool UseEarlyWaitSnapshot(bool requested, bool waitSnapshots,
		bool throttling, bool singleCallback, std::uint32_t spacingMs, bool explicitLimit = false) noexcept
    {
        return requested && waitSnapshots && throttling && (singleCallback || explicitLimit) && spacingMs == 0;
    }

    struct WaitSnapshot
    {
        Token oldest;
        std::uint64_t ageMs{}, thresholdMs{};
        std::uint32_t ordinal{}, skippedThresholds{};
    };

    // Observation only: no changes to State, callback signals or watchdogs.
    // Each threshold is used at most once per job, even if the oldest request
    // changes. A late poll consumes earlier thresholds without catch-up dumps.
    // Opt-in early observation adds one threshold; the original three remain.
    // The final timeout dump is independent of this per-job allowance.
    class WaitSnapshotSchedule
    {
    public:
        static constexpr std::array<std::uint64_t, 4> thresholds{ 5000, 30000, 60000, 90000 };
        static constexpr std::uint64_t pollIntervalMs = 1000;

        explicit WaitSnapshotSchedule(bool early = false) noexcept :
            nextThreshold(early ? 0u : 1u), earlyEnabled(early) {}

        [[nodiscard]] bool EarlyEnabled() const noexcept { return earlyEnabled; }
        [[nodiscard]] std::uint32_t Attempts() const noexcept { return attempts; }

        template<class Pending>
        [[nodiscard]] std::optional<WaitSnapshot> Poll(const State& state, std::uint64_t nowMs,
            bool enabled, Pending&& pending)
        {
            if (!enabled || state.done || nextThreshold == thresholds.size() ||
                (polled && (nowMs < lastPollMs || nowMs - lastPollMs < pollIntervalMs))) { return std::nullopt; }
            polled = true;
            lastPollMs = nowMs;
            const Slot* oldest{};
            for (const auto& slot : state.slots) {
                if (!slot.active || !slot.accepted || nowMs < slot.startedMs || !pending(slot.token.slot)) { continue; }
                if (!oldest || slot.startedMs < oldest->startedMs) { oldest = &slot; }
            }
            if (!oldest) { return std::nullopt; }
            const auto age = nowMs - oldest->startedMs;
            if (age < thresholds[nextThreshold]) { return std::nullopt; }
            const auto first = nextThreshold;
            while (nextThreshold + 1 < thresholds.size() && age >= thresholds[nextThreshold + 1]) { ++nextThreshold; }
            WaitSnapshot result{ oldest->token, age, thresholds[nextThreshold], ++attempts,
                static_cast<std::uint32_t>(nextThreshold - first) };
            ++nextThreshold;
            return result;
        }

    private:
        std::size_t nextThreshold{};
        std::uint64_t lastPollMs{};
        std::uint32_t attempts{};
        bool polled{};
        bool earlyEnabled{};
    };
}
