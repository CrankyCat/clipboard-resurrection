// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorkshopCallbackSlice.h"

template<class Bytes, class Check> void CheckWorkshopCallbackDrain(Check&& check)
{
    using namespace Clipboard::WorkshopCallbacks;
    const auto make = [](unsigned rows, int high, int low, bool on = true) {
        return State(rows, SchedulingPolicy::ContinuationsFirst, on, 4, false, high, low);
    };
    check(kCallTimeoutMs == 300000, "diagnostic observation ceiling is explicitly five minutes");
    for (const bool throttled : { true, false }) {
        auto state = make(3, 2, 0, throttled);
        const auto first = *state.Reserve(0); (void)state.Accept(first);
        const auto second = *state.Reserve(0); (void)state.Accept(second);
        state.Tick(120000);
        check(!state.done && state.outstanding == 2, "old two-minute ceiling no longer ends pending observation");
        state.Tick(240000);
        check(state.Complete(first) && !state.done && state.returned == 1 && state.outstanding == 1,
            "a four-minute actual return remains observable and advances only its own row");
        state.Tick(300000);
        check(state.timedOut == throttled,
            "five-minute On deadline stays per request; Off retains successful-return inactivity semantics");
        if (!throttled) {
            state.Tick(539999);
            check(!state.done, "Off progress extends its inactivity deadline by five minutes");
            state.Tick(540000);
            check(state.timedOut && state.outstanding == 1 && !state.Complete(second),
                "Off stops at exactly five minutes without a return and retains uncertain ownership");
        }
    }
    for (const auto low : { -1, -2, 50, 51 }) {
        auto state = make(80, 50, low);
        check(!state.drain.Enabled(), "missing or invalid drain threshold preserves rolling admission");
    }
    check(!make(80, 0, 0).drain.Enabled() && !make(80, -1, 0).drain.Enabled() &&
        !make(80, 50, 0, false).drain.Enabled() && !make(12, 50, 25).drain.Enabled(),
        "drain needs an explicit finite On limit and a low threshold below the row-bounded high");
    check(make(12, 50, 0).drain.High() == 12 && make(12, 50, 0).drain.Enabled() &&
        make(1, 1, 0).drain.Enabled(), "small and single-row jobs accept valid effective thresholds");

    for (const auto low : { 0, 25 }) {
        auto state = make(80, 50, low);
        std::vector<Token> pending;
        const auto dispatch = [&](const Token& token) {
            (void)state.Accept(token); pending.push_back(token);
        };
        auto slice = RunSlice(state, [] { return 0ULL; }, [] { return 10ULL; }, dispatch);
        check(slice.stop == SliceStop::Capacity && slice.steps == 50 && state.drain.Closed() &&
            state.drain.Closures() == 1, "full accepted window closes admission across native slices");
        const auto generation = state.nextGeneration;
        bool held = true;
        while (pending.size() > static_cast<unsigned>(low + 1)) {
            const auto token = pending.back(); pending.pop_back();
            state.Tick(100);
            held &= state.Complete(token) && !state.Complete(token);
            slice = RunSlice(state, [] { return 0ULL; }, [] { return 100ULL; }, dispatch);
            held &= slice.steps == 0 && slice.stop == SliceStop::NoReady &&
                state.nextGeneration == generation && state.dispatched == 50 && state.drain.Closed();
        }
        check(held && state.Valid(), "partial returns do not refill slots, acknowledge twice or reserve new generations");
        const auto last = pending.back(); pending.pop_back();
        state.Tick(200);
        check(state.Complete(last) && !state.drain.Closed() && state.drain.Reopenings() == 1 &&
            state.drain.LastHoldMs() == 190 && state.drain.HeldMs(200) == 190 && !state.done &&
            state.completedRows == 0 && state.outstanding == static_cast<unsigned>(low),
            "reaching the low threshold reopens admission without claiming all rows initialized");
        slice = RunSlice(state, [] { return 0ULL; }, [] { return 200ULL; }, dispatch);
        check(slice.steps == static_cast<unsigned>(50 - low) && state.outstanding == 50 &&
            state.drain.Closed() && state.drain.Closures() == 2 && state.Valid(),
            "refill can reach the high threshold again and starts a new drain interval");
        for (const auto& token : pending) {
            if (token.row >= 50) { continue; }
            check(token.phase == Phase::ObjectPlaced || state.rows[token.row].phase == Phase::WorkshopPlaced,
                "each row retains its own ordered continuation under partial-drain refill");
        }
    }
    {
        auto state = make(5, 2, 0);
        const auto first = *state.Reserve(0); (void)state.Accept(first);
        const auto noOp = *state.Reserve(0);
        check(!state.drain.Closed() && state.Omit(noOp) && state.drain.Closures() == 0,
            "reservation and verified no-op do not count as a second accepted pending call");
        const auto rejected = *state.Reserve(0);
        check(state.Reject(rejected) && !state.drain.Closed() && state.failed == 1,
            "rejected unaccepted request cannot close the drain");
        const auto second = *state.Reserve(0); (void)state.Accept(second);
        check(state.drain.Closed() && state.Complete(second, true) && state.drain.Closed() &&
            state.failed == 2 && state.returned == 0, "VM cancellation resolves ownership without faking a return");
        check(state.Complete(first) && !state.drain.Closed() && state.returned == 1 &&
            state.Summary()[0] == 0 && state.Valid(), "last return reopens admission but failed rows prevent success");
    }
    for (const auto low : { 0, 25 }) {
        // The 55-row case leaves a short tail. Reverse-order returns exercise
        // continuation queues, reused slots and repeated closes/reopens.
        auto state = make(55, 50, low);
        std::vector<Token> pending;
        std::vector<unsigned> phases(55);
        bool ordered = true;
        unsigned iterations{};
        while (!state.done && iterations++ < 221) {
            while (const auto token = state.Reserve(iterations)) {
                ordered &= static_cast<unsigned>(token->phase) == phases[token->row] && state.Accept(*token);
                pending.push_back(*token);
            }
            if (pending.empty()) { break; }
            const auto token = pending.back(); pending.pop_back();
            ordered &= state.Complete(token);
            ++phases[token.row];
        }
        check(ordered && state.done && pending.empty() && state.completedRows == 55 &&
            state.returned == 220 && state.outstanding == 0 && state.Summary()[0] == 1 && state.Valid(),
            "short final groups finish all four ordered phases without waiting to fill the high watermark");
        check(state.drain.Closures() > 1 && state.drain.Closures() == state.drain.Reopenings(),
            "successful jobs leave no closed drain interval");
    }
    {
        auto state = make(3, 2, 0);
        const auto oldest = *state.Reserve(0); (void)state.Accept(oldest);
        const auto younger = *state.Reserve(500); (void)state.Accept(younger);
        state.Tick(kCallTimeoutMs - 1); (void)state.Complete(younger);
        check(state.drain.Closed() && !state.Reserve(kCallTimeoutMs - 1) && !state.done,
            "younger return does not reopen zero-drain or advance oldest deadline");
        check(!state.Reserve(kCallTimeoutMs) && state.timedOut && state.outstanding == 1 &&
            !state.Complete(oldest) && state.drain.HeldMs(kCallTimeoutMs) == kCallTimeoutMs - 500 && state.Valid(),
            "closed admission still times out the original request and rejects late callbacks");
    }
    {
        auto state = make(3, 2, 0);
        while (const auto token = state.Reserve(0)) { (void)state.Accept(*token); }
        state.Cancel();
        check(state.cancelled && state.outstanding == 2 && state.drain.Closed() &&
            !state.Reserve(1) && state.Valid(), "cancellation retains uncertain calls even during a drain");
    }
    {
        auto drain = make(80, 50, 0), rolling = make(80, 50, -1);
        for (unsigned i = 0; i < 50; ++i) {
            (void)drain.Accept(*drain.Reserve(10)); (void)rolling.Accept(*rolling.Reserve(10));
        }
        Bytes a, b;
        check(drain.drain.Closed() && drain.Save([&](const auto& v) { return a.Write(v); }) &&
            rolling.Save([&](const auto& v) { return b.Write(v); }) && a.data == b.data,
            "runtime drain adds no fields or changed values to the existing v5 saved payload");
        State loaded;
        check(loaded.Load([&](auto& v) { return a.Read(v); }) && loaded.interrupted && loaded.done &&
            !loaded.drain.Enabled() && loaded.outstanding == 50 && !loaded.Reserve(11) && loaded.Valid(),
            "restored closed-drain job interrupts without replay or lost uncertain ownership");
    }
    {
        auto state = make(80, 50, 0);
        std::uint64_t now{};
        const auto slice = RunSlice(state, [&] { return now; }, [] { return 0ULL; }, [&](const Token& token) {
            (void)state.Accept(token); now += 150000;
        });
        check(slice.steps == 7 && slice.stop == SliceStop::TimeBudget && !state.drain.Closed(),
            "drain threshold does not override the one-millisecond native slice budget");
        auto immediate = make(80, 50, 0);
        const auto fast = RunSlice(immediate, [] { return 0ULL; }, [] { return 0ULL; }, [&](const Token& token) {
            (void)immediate.Accept(token); (void)immediate.Complete(token);
        });
        check(fast.steps == 64 && fast.stop == SliceStop::StepLimit && immediate.drain.Closures() == 0,
            "immediate returns retain the step budget without inventing a full pending group");
    }
}
