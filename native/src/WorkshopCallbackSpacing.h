// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once
#include "WorkshopCallbackState.h"
#include <string_view>

namespace Clipboard::WorkshopCallbacks
{
    // Deliberately a two-arm experiment, not an unbounded tuning control.
    inline std::uint32_t ParseCallbackSpacing(std::string_view value) noexcept
    {
        return value == "100" ? 100u : 0u;
    }

    inline bool IsWorkshopPhase(Phase phase) noexcept
    {
        return phase == Phase::WorkshopPlaced || phase == Phase::WorkshopMoved;
    }

    // Transient job-local policy. Never serialized or consulted by restored jobs.
    // The deadline starts at observation of an actual successful placed return;
    // object-side work can consume it. It is not a listener-drain barrier.
    struct CallbackSpacing
    {
        explicit CallbackSpacing(std::uint32_t requested = 0, bool throttling = false, bool single = false) :
            requestedMs(requested), effectiveMs(requested == 100 && throttling && single ? 100u : 0u) {}

        void Returned(Phase phase, std::uint64_t observedMs) noexcept
        {
            if (effectiveMs && phase == Phase::WorkshopPlaced) {
                FinishWait(observedMs);
                deadlineMs = observedMs > UINT64_MAX - effectiveMs ? UINT64_MAX : observedMs + effectiveMs;
                ++armed;
            }
        }

        bool Allows(Phase phase, std::uint64_t nowMs) noexcept
        {
            if (!effectiveMs || !IsWorkshopPhase(phase)) { return true; }
            if (nowMs >= deadlineMs) { FinishWait(nowMs); return true; }
            if (!blockedSince) { blockedSince = nowMs; ++heldAdmissions; }
            return false;
        }

        void FinishWait(std::uint64_t nowMs) noexcept
        {
            if (!blockedSince) { return; }
            const auto end = std::min(nowMs, deadlineMs);
            if (end >= *blockedSince) { heldMs += end - *blockedSince; }
            blockedSince.reset();
        }

        std::uint32_t requestedMs{}, effectiveMs{};
        std::uint64_t deadlineMs{}, armed{}, heldAdmissions{}, heldMs{};
        std::optional<std::uint64_t> blockedSince;
    };

    // Constant storage and no per-return I/O. Durations include VM queue time
    // and delay until Clipboard observes the signal, not just script execution.
    struct CallbackLatency
    {
        void Record(const Token& token, std::uint64_t startedMs, std::uint64_t observedMs) noexcept
        {
            const auto ms = observedMs >= startedMs ? observedMs - startedMs : 0;
            ++count;
            totalMs += ms;
            if (count == 1 || ms > maximumMs) { maximumMs = ms; slowest = token; }
            const auto bucket = ms < 100 ? 0u : ms < 1000 ? 1u : ms < 10000 ? 2u : ms < 30000 ? 3u : ms < 60000 ? 4u : 5u;
            ++buckets[bucket];
        }
        std::uint64_t count{}, totalMs{}, maximumMs{};
        Token slowest;
        std::array<std::uint64_t, 6> buckets{};
    };
}
