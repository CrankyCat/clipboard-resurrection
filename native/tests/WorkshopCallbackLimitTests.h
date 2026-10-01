// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorkshopCallbackLimit.h"
#include "WorkshopCallbackSlice.h"
#include "WorkshopWaitSnapshots.h"

template<class Bytes, class Check> void CheckWorkshopCallbackLimit(Check&& check)
{
    using namespace Clipboard::WorkshopCallbacks;
    check(ParseCallbackLimit("0") == 0 && ParseCallbackLimit("50") == 50 &&
        ParseCallbackLimit("262144") == 262144, "callback limit accepts all, 50 and the hard row bound");
    for (const auto value : { "", "-1", "-50", "+50", "50x", "1.5", "262145", "4294967296", " 50" }) {
        check(ParseCallbackLimit(value) == -1, "invalid callback limits preserve legacy admission instead of enabling all");
    }
    State normal(90), single(90, SchedulingPolicy::ContinuationsFirst, true, 4, true);
    State fifty(90, SchedulingPolicy::ContinuationsFirst, true, 4, true, 50);
    State all(90, SchedulingPolicy::ContinuationsFirst, true, 4, true, 0);
    State off(90, SchedulingPolicy::ContinuationsFirst, false, 4, true, 50);
    check(normal.AdmissionLimit() == 4 && single.AdmissionLimit() == 1 && fifty.AdmissionLimit() == 50 &&
        all.AdmissionLimit() == 90 && all.UsesThrottling() && off.AdmissionLimit() == 90 && !off.UsesThrottling(),
        "explicit limit overrides old single policy, keeps On budget and leaves Off unchanged");
    State small(12, SchedulingPolicy::ContinuationsFirst, true, 4, false, 50);
    State empty(0, SchedulingPolicy::ContinuationsFirst, true, 4, false, 0);
    check(small.Capacity() == 12 && empty.Capacity() == 1 && empty.done && empty.Valid(),
        "window is bounded by physical rows and empty jobs remain valid terminal results");
    check(UseEarlyWaitSnapshot(true, true, true, false, 0, true) &&
        !UseEarlyWaitSnapshot(true, true, false, false, 0, true) &&
        !UseEarlyWaitSnapshot(true, true, true, false, 100, true),
        "explicit window keeps early observation under On and zero spacing, never Off");

    for (const auto limit : { 1, 4, 50, 0 }) {
        State state(80, SchedulingPolicy::ContinuationsFirst, true, 4, false, limit);
        std::vector<Token> held;
        std::vector<unsigned> phases(80);
        bool ordered = true;
        while (const auto token = state.Reserve(0)) { ordered &= state.Accept(*token); held.push_back(*token); }
        const auto peak = limit == 0 ? 80u : static_cast<unsigned>(limit);
        check(held.size() == peak && !state.done && state.returned == 0 && !state.Reserve(1),
            "filling a callback window never opens the final completion barrier");
        // Complete in reverse order: each row's continuation must retain its own
        // phase and generation even though other rows have not returned yet.
        const auto original = held.back(); held.pop_back();
        ordered &= state.Complete(original) && !state.Complete(original);
        ++phases[original.row];
        const auto continuation = *state.Reserve(1);
        ordered &= continuation.row == original.row && continuation.phase == Phase::ObjectPlaced &&
            continuation.generation != original.generation && state.Accept(continuation) && !state.Complete(original);
        held.push_back(continuation);
        check(ordered && state.outstanding == peak && state.returned == 1 && !state.done,
            "a return refills one slot immediately without waiting for a whole batch or reordering an object");
        unsigned iterations{};
        while (!held.empty() && iterations++ < 321) {
            const auto token = held.back(); held.pop_back();
            ordered &= static_cast<unsigned>(token.phase) == phases[token.row]++ && state.Complete(token);
            while (const auto next = state.Reserve(1)) { ordered &= state.Accept(*next); held.push_back(*next); }
        }
        check(ordered && state.done && state.completedRows == 80 && state.returned == 320 &&
            state.outstanding == 0 && state.peakOutstanding == peak && state.Summary()[0] == 1 && state.Valid(),
            "all required returns close the barrier with exact accounting at variable window widths");
    }
    {
        State state(100, SchedulingPolicy::ContinuationsFirst, true, 4, false, 50);
        std::uint64_t now{};
        const auto slice = RunSlice(state, [&] { return now; }, [] { return 0ULL; }, [&](const Token& token) {
            (void)state.Accept(token); now += 150000;
        });
        check(slice.stop == SliceStop::TimeBudget && slice.steps == 7 && state.outstanding == 7 && state.Valid(),
            "50-call limit retains the one-millisecond native slice budget");
        state.Cancel();
        check(state.cancelled && state.outstanding == 7 && !state.Reserve(1) && state.Valid(),
            "wide-window cancellation retains uncertain calls and stops new admission");
    }
    {
        State state(100, SchedulingPolicy::ContinuationsFirst, true, 4, false, 0);
        const auto slice = RunSlice(state, [] { return 0ULL; }, [] { return 0ULL; }, [&](const Token& token) {
            (void)state.Accept(token); (void)state.Complete(token);
        });
        check(slice.stop == SliceStop::StepLimit && slice.steps == 64 && state.completedRows == 16 && !state.done,
            "zero removes only the admission cap, not the 64-step throttle safeguard");
    }
    for (const auto limit : { 50, 0 }) {
        State state(100, SchedulingPolicy::ContinuationsFirst, true, 4, false, limit);
        const auto older = *state.Reserve(0); (void)state.Accept(older);
        const auto younger = *state.Reserve(500); (void)state.Accept(younger);
        state.Tick(kCallTimeoutMs - 1); (void)state.Complete(younger);
        check(!state.done && state.returned == 1, "wide-window pending request retains exact original deadline");
        state.Tick(kCallTimeoutMs);
        check(state.timedOut && state.outstanding == 1 && !state.Complete(older) && !state.Reserve(kCallTimeoutMs + 1) && state.Valid(),
            "another return never postpones On-mode per-request timeout, including zero cap");
    }
    {
        State state(9685, SchedulingPolicy::ContinuationsFirst, true, 4, false, 50);
        (void)state.SkipRow(0);
        const auto failed = *state.Reserve(0); (void)state.Reject(failed);
        bool ordered = true;
        for (unsigned row = 2; row < 4; ++row) {
            for (unsigned phase = 0; phase < 4; ++phase) {
                const auto token = *state.Reserve(0);
                ordered &= token.row == row && static_cast<unsigned>(token.phase) == phase;
                if (phase == 0 || phase == 2) { ordered &= state.Omit(token); }
                else { ordered &= state.Accept(token) && state.Complete(token); }
            }
        }
        while (const auto token = state.Reserve(0)) { (void)state.Accept(*token); }
        Bytes saved;
        check(ordered && state.skippedRows == 1 && state.failed == 1 && state.omittedCalls == 4 &&
            state.completedRows == 2 && state.returned == 4 && state.outstanding == 50 && state.Valid() &&
            state.Save([&](const auto& v) { return saved.Write(v); }),
            "pattern98-sized state preserves omissions, failures and 50 real pending calls");
        for (const auto version : { 1u, 2u, 3u, 4u }) {
            Bytes old;
            check(!state.Save([&](const auto& v) { return old.Write(v); }, version) && old.data.empty(),
                "wide state cannot be mislabeled as any predecessor payload");
        }
        auto legacy = saved;
        State prior(2);
        check(!prior.Load([&](auto& v) { return legacy.Read(v); }, 4) && prior.rows.size() == 2,
            "v4 reader continues to reject wider capacities");
        const auto stateEnd = saved.data.size();
        for (unsigned i = 0; i < 50; ++i) { saved.Write(i); saved.Write(100u + i); }
        State restored;
        check(restored.Load([&](auto& v) { return saved.Read(v); }) && saved.cursor == stateEnd &&
            restored.Capacity() == 50 && restored.outstanding == 50 && restored.done && restored.interrupted &&
            !restored.Reserve(1) && restored.Valid(), "v5 unfinished load retains 50 uncertain calls and never replays");
        bool aligned = true;
        for (unsigned i = 0; i < 50; ++i) {
            unsigned target{}, argument{};
            aligned &= saved.Read(target) && saved.Read(argument) && target == i && argument == 100 + i;
        }
        check(aligned && saved.cursor == saved.data.size(), "v5 wider trailing dispatch identities stay aligned");
        Bytes resaved; State reread;
        check(restored.Save([&](const auto& v) { return resaved.Write(v); }) &&
            reread.Load([&](auto& v) { return resaved.Read(v); }) && reread.Summary() == restored.Summary(),
            "interrupted wide state can be resaved without losing uncertain ownership");
    }
    {
        State state(kMaximumRows, SchedulingPolicy::ContinuationsFirst, true, 4, false, 0);
        std::uint32_t issued{}; bool indexed = true;
        while (const auto token = state.Reserve(0)) {
            indexed &= token->slot == token->row; (void)state.Accept(*token); ++issued;
        }
        check(indexed && issued == kMaximumRows && state.UsesRowSlots() && state.Valid() && !state.done,
            "maximum all-row On window uses row-index ownership without free-slot rescans");
        WaitSnapshotSchedule plan(true);
        const auto shot = plan.Poll(state, 5000, true, [](auto) { return true; });
        check(shot && shot->oldest.row == 0 && state.returned == 0 && plan.Attempts() == 1,
            "wide snapshot remains a bounded observation and does not complete calls");
    }
}
