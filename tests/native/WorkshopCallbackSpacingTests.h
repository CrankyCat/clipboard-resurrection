// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorkshopCallbackSpacing.h"
#include "WorkshopCallbackSlice.h"

template<class Bytes, class Check> void CheckWorkshopCallbackSpacing(Check&& check)
{
    using namespace Clipboard::WorkshopCallbacks;
    check(ParseCallbackSpacing("100") == 100 && ParseCallbackSpacing("0") == 0 &&
        ParseCallbackSpacing("") == 0 && ParseCallbackSpacing("-100") == 0 &&
        ParseCallbackSpacing("1000") == 0 && ParseCallbackSpacing("100junk") == 0,
        "spacing accepts only the bounded comparison value, malformed values are baseline");
    for (const bool throttle : {false, true}) {
        for (const bool single : {false, true}) {
            for (const auto requested : {0u, 100u, 101u}) {
                CallbackSpacing gap(requested, throttle, single);
                gap.Returned(Phase::WorkshopPlaced, 10);
                check(!gap.Allows(Phase::WorkshopMoved, 10) == (requested == 100 && throttle && single),
                    "only explicit 100ms with On/single changes admission; normal and Off are unchanged");
            }
        }
    }
    {
        State state(2, SchedulingPolicy::ContinuationsFirst, true, 4, true);
        CallbackSpacing gap(100, true, true);
        std::uint64_t now = 10;
        const auto admit = [&](std::uint32_t, Phase phase, std::uint64_t time) { return gap.Allows(phase, time); };
        const auto complete = [&](const Token& token, bool cancelled = false) {
            if (state.Complete(token, cancelled) && !cancelled) { gap.Returned(token.phase, now); }
        };
        auto placed = *state.Reserve(now, admit);
        (void)state.Accept(placed);
        CompletionSignal signal;
        signal.Resolve(Completion::Returned);
        check(!state.Reserve(now, admit) && gap.armed == 0,
            "resolved but unconsumed signal cannot release ownership or arm spacing");
        now = 20;
        complete(placed);
        auto object = *state.Reserve(now, admit);
        check(object.phase == Phase::ObjectPlaced && gap.armed == 1,
            "object placed proceeds immediately during workshop cooldown");
        (void)state.Accept(object);
        now = 60;
        complete(object);
        state.Tick(now);
        Bytes before;
        (void)state.Save([&](const auto& v) { return before.Write(v); });
        unsigned dispatches{};
        auto slice = RunSlice(state, [] { return 0ULL; }, [&] { return now; }, [&](const Token&) { ++dispatches; }, admit);
        Bytes after;
        (void)state.Save([&](const auto& v) { return after.Write(v); });
        check(slice.steps == 0 && dispatches == 0 && state.outstanding == 0 && before.data == after.data && state.Valid(),
            "cooldown holds before reservation with no accepted slot, generation change or saved state change");
        now = 119;
        check(!state.Reserve(now, admit) && !state.TimeoutCheckDue(now), "workshop moved waits until observed return plus 100ms");
        now = 120;
        const auto moved = *state.Reserve(now, admit);
        check(moved.row == 0 && moved.phase == Phase::WorkshopMoved && gap.heldMs == 60 && gap.heldAdmissions == 1,
            "same row resumes exactly at deadline; object overlap consumes 40ms and idle counts once");
        (void)state.Accept(moved);
        state.Tick(kCallTimeoutMs + 119);
        check(!state.done, "accepted moved request gets its complete five-minute watchdog after the gap");
        state.Tick(kCallTimeoutMs + 120);
        check(state.timedOut && state.outstanding == 1 && !state.Reserve(kCallTimeoutMs + 121, admit), "watchdog expiry is unchanged and does not replay requests");
        complete(moved);
        complete(placed);
        check(gap.armed == 1 && state.returned == 2, "late and duplicate completions cannot rearm cooldown or change timed-out accounting");
    }
    {
        CallbackSpacing gap(100, true, true);
        gap.Returned(Phase::WorkshopPlaced, 0);
        check(gap.Allows(Phase::ObjectPlaced, 0) && gap.Allows(Phase::WorkshopMoved, 250) && gap.heldAdmissions == 0,
            "slow object callback consumes the entire gap without adding wait");
        gap.Returned(Phase::WorkshopMoved, 250);
        gap.Returned(Phase::ObjectMoved, 250);
        check(gap.Allows(Phase::WorkshopPlaced, 250) && gap.armed == 1, "moved and object returns do not introduce extra gaps");
        gap.Returned(Phase::WorkshopPlaced, 300);
        (void)gap.Allows(Phase::WorkshopMoved, 310);
        check(gap.Allows(Phase::WorkshopMoved, 1000) && gap.heldMs == 90,
            "idle excludes time after the deadline when engine scheduling overshoots");
    }
    {
        State state(3, SchedulingPolicy::ContinuationsFirst, true, 4, true);
        CallbackSpacing gap(100, true, true);
        const auto omitted = *state.Reserve(0);
        (void)state.Omit(omitted);
        const auto rejected = *state.Reserve(0);
        (void)state.Reject(rejected);
        const auto cancelled = *state.Reserve(0);
        (void)state.Accept(cancelled);
        if (state.Complete(cancelled, true)) { /* cancelled returns never arm the policy */ }
        check(gap.armed == 0 && gap.Allows(Phase::WorkshopPlaced, 0) && state.failed == 2 && state.omittedCalls == 1,
            "omission, rejection and cancellation remain distinct from successful callback returns");
        const auto placed = *state.Reserve(10);
        (void)state.Accept(placed); (void)state.Complete(placed); gap.Returned(placed.phase, 20);
        const auto object = *state.Reserve(20);
        (void)state.Accept(object); (void)state.Complete(object);
        (void)gap.Allows(Phase::WorkshopMoved, 30);
        Bytes bytes;
        (void)state.Save([&](const auto& v) { return bytes.Write(v); });
        State loaded;
        check(loaded.Load([&](auto& v) { return bytes.Read(v); }) && loaded.interrupted && loaded.done && !loaded.Reserve(40),
            "loading during cooldown interrupts unfinished work with unchanged v4 payload and no replay");
        state.Cancel(); gap.FinishWait(40);
        check(!state.Reserve(120) && gap.heldMs == 10 && state.outstanding == 0,
            "cancellation ends cooldown accounting immediately without synthetic pending work");
    }
    for (const auto spacing : {0u, 100u}) {
        State state(2, SchedulingPolicy::ContinuationsFirst, true, 4, true);
        CallbackSpacing gap(spacing, true, true);
        std::uint64_t now{};
        unsigned issued{};
        bool ordered = true;
        while (!state.done && now < 1000) {
            (void)RunSlice(state, [] { return 0ULL; }, [&] { return now; }, [&](const Token& token) {
                ordered &= token.row == issued / 4 && static_cast<unsigned>(token.phase) == issued % 4;
                ++issued;
                (void)state.Accept(token);
                if (state.Complete(token)) { gap.Returned(token.phase, now); }
            }, [&](std::uint32_t, Phase phase, std::uint64_t time) { return gap.Allows(phase, time); });
            if (!state.done) { ++now; }
        }
        check(state.done && state.returned == 8 && issued == 8 && state.peakOutstanding == 1 && ordered && state.Valid() &&
            now == (spacing ? 200u : 0u) && gap.heldMs == now,
            "synchronous returns preserve four-phase physical order and apply one bounded gap per actual placed return");
    }
    {
        CallbackLatency stats;
        for (const auto ms : {99u, 100u, 999u, 1000u, 9999u, 10000u, 29999u, 30000u, 59999u, 60000u}) {
            stats.Record(Token{ms, Phase::WorkshopMoved, 0, ms}, 50, 50 + ms);
        }
        check(stats.count == 10 && stats.maximumMs == 60000 && stats.slowest.row == 60000 &&
            stats.buckets == std::array<std::uint64_t, 6>{1,2,2,2,2,1}, "latency observations use non-overlapping bounded histogram buckets");
    }
}
