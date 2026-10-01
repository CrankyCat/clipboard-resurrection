// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorkshopCallbackSlice.h"
#include "WorkshopWaitSnapshots.h"

template<class Bytes, class Check> void CheckWorkshopSingleCallback(Check&& check)
{
    using namespace Clipboard::WorkshopCallbacks;
    {
        State state(3, SchedulingPolicy::ContinuationsFirst, true, kDefaultOutstanding, true);
        std::optional<Token> held;
        bool ordered = true;
        unsigned issued{};
        while (!state.done && issued < 13) {
            const auto slice = RunSlice(state, [] { return 0ULL; }, [] { return 0ULL; }, [&](const Token& token) {
                ordered &= !held && token.row == issued / 4 && static_cast<unsigned>(token.phase) == issued % 4;
                ordered &= state.Accept(token);
                held = token;
                ++issued;
            });
            check(slice.steps == 1 && slice.stop == SliceStop::Capacity && state.outstanding == 1 &&
                state.returned == issued - 1 && state.Capacity() == 4 && state.AdmissionLimit() == 1 && state.Valid(),
                "diagnostic holds one actual callback across all rows/phases while retaining four storage slots");
            const auto blocked = RunSlice(state, [] { return 0ULL; }, [] { return 0ULL; }, [&](const Token&) { ordered = false; });
            CompletionSignal signal;
            signal.Resolve(Completion::Returned);
            check(blocked.steps == 0 && blocked.stop == SliceStop::Capacity && !state.Reserve(0),
                "queueing and a returned signal cannot admit another call until its actual completion is consumed");
            ordered &= signal.Get() == Completion::Returned && state.Complete(*held);
            ordered &= !state.Complete(*held);
            held.reset();
        }
        check(ordered && state.done && issued == 12 && state.returned == 12 && state.completedRows == 3 &&
            state.peakOutstanding == 1 && state.Summary()[0] == 1 && state.Valid(),
            "single-callback mode completes every row in order, ignores duplicate returns and reports true peak one");
        Bytes completed;
        State restored;
        check(state.Save([&](const auto& v) { return completed.Write(v); }) &&
            restored.Load([&](auto& v) { return completed.Read(v); }) && restored.Summary() == state.Summary() &&
            restored.Capacity() == 4 && restored.AdmissionLimit() == 4 && !restored.Reserve(0),
            "completed diagnostic save retains v4 results and remains terminal without serializing admission policy");
    }
    {
        State state(5, SchedulingPolicy::ContinuationsFirst, true, 4, true);
        const auto token = *state.Reserve(100);
        (void)state.Accept(token);
        WaitSnapshotSchedule snapshots;
        const auto observation = snapshots.Poll(state, 30100, true, [](auto) { return true; });
        check(observation && observation->oldest == token && state.outstanding == 1 && state.returned == 0,
            "wait snapshots observe the same single accepted request without acknowledging it");
        state.Tick(kCallTimeoutMs + 99);
        check(!state.done && !state.Reserve(kCallTimeoutMs + 99), "single request remains pending immediately before original deadline");
        state.Tick(kCallTimeoutMs + 100);
        check(state.timedOut && state.done && state.outstanding == 1 && !state.Reserve(kCallTimeoutMs + 101) &&
            !state.Complete(token) && state.Summary()[0] == 0 && state.Valid(),
            "five-minute watchdog stops admission and retains uncertain callback after diagnostic timeout");
    }
    {
        State state(3, SchedulingPolicy::ContinuationsFirst, true, 4, true);
        State normal(3);
        const auto token = *state.Reserve(10);
        const auto normalToken = *normal.Reserve(10);
        (void)state.Accept(token); (void)normal.Accept(normalToken);
        Bytes diagnosticBytes, normalBytes;
        check(state.Save([&](const auto& v) { return diagnosticBytes.Write(v); }) &&
            normal.Save([&](const auto& v) { return normalBytes.Write(v); }) && diagnosticBytes.data == normalBytes.data,
            "identical pending state has byte-identical v4 payload in diagnostic and normal modes");
        const auto stateEnd = diagnosticBytes.data.size();
        for (unsigned slot = 0; slot < 4; ++slot) { diagnosticBytes.Write(100u + slot); diagnosticBytes.Write(200u + slot); }
        State loaded(9, SchedulingPolicy::ContinuationsFirst, true, 4, true);
        check(loaded.Load([&](auto& v) { return diagnosticBytes.Read(v); }) && loaded.Capacity() == 4 &&
            diagnosticBytes.cursor == stateEnd && loaded.done && loaded.interrupted && loaded.outstanding == 1 &&
            loaded.returned == 0 && !loaded.Reserve(11) && !loaded.Complete(token) && loaded.Valid(),
            "unfinished diagnostic load interrupts without replay and retains the four trailing dispatch ID pairs");
        bool aligned = true;
        for (unsigned slot = 0; slot < 4; ++slot) {
            unsigned target{}, argument{};
            aligned &= diagnosticBytes.Read(target) && diagnosticBytes.Read(argument) && target == 100 + slot && argument == 200 + slot;
        }
        check(aligned && diagnosticBytes.cursor == diagnosticBytes.data.size(), "diagnostic saves do not shift dispatch metadata");
        state.Cancel();
        check(state.cancelled && state.outstanding == 1 && !state.Complete(token) && !state.Reserve(11) && state.Valid(),
            "cancellation preserves the pending diagnostic request and blocks admission");
    }
    {
        State state(4, SchedulingPolicy::ContinuationsFirst, true, 4, true);
        (void)state.SkipRow(0);
        const auto bad = *state.Reserve(0);
        (void)state.Reject(bad);
        while (const auto token = state.Reserve(0)) {
            if (token->phase == Phase::WorkshopPlaced || token->phase == Phase::WorkshopMoved) { (void)state.Omit(*token); }
            else { (void)state.Accept(*token); (void)state.Complete(*token); }
        }
        check(state.done && state.Valid() && state.skippedRows == 1 && state.failed == 1 && state.completedRows == 2 &&
            state.returned == 4 && state.omittedCalls == 4 && state.peakOutstanding == 1 && state.Summary()[0] == 0,
            "single mode retains failed-row exclusion and no-op omissions without treating them as returned calls");
    }
    for (const auto cost : { 1ULL, 600000ULL }) {
        State state(100, SchedulingPolicy::ContinuationsFirst, true, 4, true);
        std::uint64_t now{};
        const auto slice = RunSlice(state, [&] { return now; }, [] { return 0ULL; }, [&](const Token& token) {
            (void)state.Accept(token); (void)state.Complete(token); now += cost;
        });
        check(state.Valid() && state.peakOutstanding == 1 &&
            (cost == 1 ? slice.steps == 64 && slice.stop == SliceStop::StepLimit : slice.steps == 2 && slice.stop == SliceStop::TimeBudget),
            "synchronous diagnostic calls retain 64-step and 1ms slice limits without an artificial inter-call delay");
    }
    for (const bool single : { false, true }) {
        State state(9, SchedulingPolicy::ContinuationsFirst, false, 4, single);
        unsigned issued{};
        while (const auto token = state.Reserve(0)) { (void)state.Accept(*token); ++issued; }
        check(issued == 9 && state.AdmissionLimit() == 9 && state.outstanding == 9 && state.Valid(),
            "Throttling Off ignores single-callback diagnostic and preserves original ready-work admission");
    }
    {
        State state(9);
        unsigned issued{};
        while (const auto token = state.Reserve(0)) { (void)state.Accept(*token); ++issued; }
        check(issued == 4 && state.AdmissionLimit() == 4 && state.Valid(), "normal default remains four outstanding callbacks");
    }
}
