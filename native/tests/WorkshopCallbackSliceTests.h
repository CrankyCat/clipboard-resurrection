// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorkshopCallbackSlice.h"
#include <vector>

template<class Check> void CheckWorkshopCallbackSlices(Check&& check)
{
    using namespace Clipboard::WorkshopCallbacks;
    {
        State state{ 100, SchedulingPolicy::ContinuationsFirst, false };
        unsigned budgetReads{}, ageReads{}, admissionReads{}, dispatched{};
        std::uint64_t age{};
        RunSlice<false>(state, [&] { ++budgetReads; return 0ULL; }, [&] { ++ageReads; return age; }, [&](const Token& token) {
            (void)state.Accept(token);
            ++dispatched;
            // No callback returns: the unrestricted loop must still stop at
            // the five-minute inactivity deadline, without its budget clock.
            age += kCallTimeoutMs / 2;
        }, [&](std::uint32_t, Phase, std::uint64_t) { ++admissionReads; return false; });
        check(budgetReads == 0 && admissionReads == 0 && ageReads == 3 && dispatched == 2 &&
            state.timedOut && state.outstanding == 2 && state.returned == 0 && state.Valid(),
            "production Off skips slice clocks and admission policy but preserves the inactivity watchdog and pending ownership");
    }
    {
        State state{ 100 };
        unsigned budgetReads{}, dispatched{};
        std::uint64_t now{};
        RunSlice<false>(state, [&] { ++budgetReads; return now; }, [] { return 0ULL; }, [&](const Token& token) {
            (void)state.Accept(token); (void)state.Complete(token);
            ++dispatched;
            now += 600000;
        });
        check(budgetReads == 4 && dispatched == 2 && !state.done && state.Valid(),
            "production On retains the 1ms slice deadline without generating an unused SliceResult");
    }
    {
        State state{ 100 };
        unsigned dispatched{};
        RunSlice<false>(state, [] { return 0ULL; }, [] { return 0ULL; }, [&](const Token& token) {
            (void)state.Accept(token); (void)state.Complete(token); ++dispatched;
        });
        check(dispatched == 64 && !state.done && state.Valid(),
            "production On retains the 64-step ceiling when slice-result capture is disabled");
    }
    {
        State state{31};
        std::uint64_t now{};
        auto run = [&] {
            const auto slice = RunSlice(state, [&] { return now; }, [] { return 0ULL; }, [&](const Token& token) {
                if (token.phase == Phase::WorkshopPlaced || token.phase == Phase::WorkshopMoved) { (void)state.Omit(token); }
                else { (void)state.Accept(token); (void)state.Complete(token); }
                now += 1000;
            });
            return slice;
        };
        const auto first = run();
        const auto last = run();
        check(first.steps == 64 && first.stop == SliceStop::StepLimit && last.steps == 60 && last.stop == SliceStop::Complete,
            "31 immediate plain rows need two bounded slices instead of 31 four-step slices");
        check(state.Valid() && state.done && state.returned == 62 && state.omittedCalls == 62 && state.peakOutstanding == 1 &&
            first.steps + last.steps == 124 && first.returned + last.returned == 62 && first.omitted + last.omitted == 62,
            "larger batches preserve separate real-return and omission counts without adding outstanding work");
    }
    {
        State state{100};
        std::uint64_t now{};
        const auto slice = RunSlice(state, [&] { return now; }, [] { return 0ULL; }, [&](const Token& token) {
            (void)state.Accept(token); (void)state.Complete(token); now += 600000;
        });
        check(slice.steps == 2 && slice.stop == SliceStop::TimeBudget && slice.elapsedNs == 1200000 && state.Valid(),
            "time budget yields after an expensive step and never begins the next step after its deadline");
    }
    {
        State state{100};
        const auto slice = RunSlice(state, [] { return 0ULL; }, [] { return 0ULL; }, [&](const Token& token) { (void)state.Accept(token); });
        check(slice.steps == 4 && slice.stop == SliceStop::Capacity && state.outstanding == 4 && state.returned == 0 && state.Valid(),
            "pending callbacks retain all four slots and cannot be treated as immediate work");
    }
    {
        State state{2};
        const auto slice = RunSlice(state, [] { return 0ULL; }, [] { return 0ULL; }, [&](const Token& token) {
            (void)state.Accept(token); state.Cancel();
        });
        check(slice.steps == 1 && slice.stop == SliceStop::Complete && state.outstanding == 1 && state.Valid() && state.Summary()[0] == -1,
            "cancellation halts the batch while preserving an uncertain callback");
    }
    {
        State state{1};
        unsigned calls{};
        const auto slice = RunSlice(state, [] { return 0ULL; }, [] { return 0ULL; }, [&](const Token& token) {
            ++calls; (void)state.Reject(token);
        });
        check(calls == 1 && slice.returned == 0 && state.failed == 1 && state.Valid() && state.Summary()[0] == 0,
            "dispatch rejection never becomes a success in a larger slice");
    }
    {
        State state{1327};
        std::vector<Token> pending;
        std::vector<unsigned> next(1327);
        std::uint64_t now{};
        bool ordered = true, valid = true;
        unsigned slices{};
        while (!state.done && slices < 10000) {
            // Deliberately return newest work first; continuations may overtake
            // other rows but never their own preceding phase.
            if (!pending.empty()) { (void)state.Complete(pending.back()); pending.pop_back(); }
            (void)RunSlice(state, [&] { return now; }, [&] { return now / 1000000; }, [&](const Token& token) {
                ordered = ordered && next[token.row]++ == static_cast<unsigned>(token.phase);
                if (token.row >= 34 && (token.phase == Phase::WorkshopPlaced || token.phase == Phase::WorkshopMoved)) {
                    (void)state.Omit(token);
                } else {
                    (void)state.Accept(token);
                    if (token.row < 34) { pending.push_back(token); }
                    else { (void)state.Complete(token); }
                }
                now += 1000;
            });
            valid = valid && state.Valid() && pending.size() <= 4;
            now += 16000000;
            ++slices;
        }
        check(valid && ordered && state.done && state.completedRows == 1327 && state.returned == 2722 && state.omittedCalls == 2586 &&
            state.peakOutstanding == 4 && state.outstanding == 0 && state.Summary()[0] == 1,
            "mixed 1327-row batch preserves phase order, four pending callbacks and all required completion barriers");
    }
}
