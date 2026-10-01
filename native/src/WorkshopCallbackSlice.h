// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once
#include "WorkshopCallbackState.h"
#include <type_traits>

namespace Clipboard::WorkshopCallbacks
{
    inline constexpr std::uint32_t kMaximumStepsPerSlice = 64;
    inline constexpr std::uint64_t kSliceBudgetNs = 1000000;

    enum class SliceStop : std::uint8_t { Complete, Capacity, StepLimit, TimeBudget, NoReady, Count };
    struct SliceResult
    {
        std::uint32_t steps{}, returned{}, omitted{};
        std::uint64_t elapsedNs{};
        SliceStop stop{};
    };

    // Dispatch must consume actual completions before returning. An omitted or
    // immediately returned phase can reuse a slot, but pending work still owns
    // that slot. The job's snapshotted mode controls concurrency; disabling the
    // four-call cap never permits speculative completion or reorders a row.
    template<bool CaptureResult = true, class Clock, class Age, class Dispatch, class Admission = AllowAdmission>
    std::conditional_t<CaptureResult, SliceResult, void> RunSlice(State& state, Clock&& clock, Age&& age, Dispatch&& dispatch, Admission&& admit = {})
    {
        SliceResult result;
        const bool throttled = state.UsesThrottling();
        // Off has no slice deadline. Its separate age source remains live for
        // the inactivity watchdog; never replace that source with this clock.
        const auto start = throttled ? clock() : 0;
        [[maybe_unused]] std::uint32_t returnedBefore{}, omittedBefore{};
        if constexpr (CaptureResult) {
            returnedBefore = state.returned;
            omittedBefore = state.omittedCalls;
        }
        for (;;) {
            if (throttled) {
                const auto now = clock();
                result.elapsedNs = now >= start ? now - start : kSliceBudgetNs;
            }
            if (state.done) { if constexpr (CaptureResult) { result.stop = SliceStop::Complete; } break; }
            if (state.outstanding >= state.AdmissionLimit()) { if constexpr (CaptureResult) { result.stop = SliceStop::Capacity; } break; }
            if (throttled && result.steps >= kMaximumStepsPerSlice) { if constexpr (CaptureResult) { result.stop = SliceStop::StepLimit; } break; }
            if (throttled && result.elapsedNs >= kSliceBudgetNs) { if constexpr (CaptureResult) { result.stop = SliceStop::TimeBudget; } break; }
            const auto token = state.Reserve(age(), admit);
            if (!token) { if constexpr (CaptureResult) { result.stop = state.done ? SliceStop::Complete : SliceStop::NoReady; } break; }
            if (throttled || CaptureResult) { ++result.steps; }
            dispatch(*token);
        }
        if constexpr (CaptureResult) {
            result.returned = state.returned - returnedBefore;
            result.omitted = state.omittedCalls - omittedBefore;
            return result;
        }
    }
}
