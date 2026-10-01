// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "WorkshopCallbackSlice.h"
#include "WorkshopProgress.h"
#include <cstring>
#include <vector>

template<class Bytes, class Check> void CheckWorkshopThrottling(Check&& check)
{
    using namespace Clipboard::WorkshopCallbacks;
    for (const bool throttled : { true, false }) {
        for (const auto count : { 0u, 1u, 129u, 1327u }) {
            State state(count, SchedulingPolicy::ContinuationsFirst, throttled);
            std::vector<Token> pending;
            std::vector<unsigned> completedPhases(count);
            bool ordered = true;
            while (const auto token = state.Reserve(0)) {
                ordered &= token->phase == Phase::WorkshopPlaced && state.Accept(*token);
                pending.push_back(*token);
            }
            const auto limit = throttled ? kDefaultOutstanding : std::max(1u, count);
            check(state.UsesThrottling() == throttled && state.Capacity() == limit &&
                pending.size() == std::min(count, limit) && state.returned == 0 && state.Valid() &&
                (count == 0 ? state.Summary()[0] == 1 : state.Summary()[0] == 0),
                "On holds at four; Off admits one pending phase per physical row and still waits for real returns");
            while (!pending.empty()) {
                const auto previous = pending.back();
                pending.pop_back();
                ordered &= state.Complete(previous);
                ++completedPhases[previous.row];
                ordered &= !state.Complete(previous);
                while (const auto token = state.Reserve(1)) {
                    ordered &= static_cast<unsigned>(token->phase) == completedPhases[token->row] && state.Accept(*token);
                    pending.push_back(*token);
                }
            }
            check(ordered && state.done && state.Valid() && state.Summary()[0] == 1 && state.outstanding == 0 &&
                state.returned == count * 4 && state.completedRows == count &&
                std::all_of(completedPhases.begin(), completedPhases.end(), [](auto n) { return n == 4; }),
                "both throttle modes preserve every row phase and final actual-return barrier with reverse-order completions");
            Bytes saved;
            State restored;
            check(state.Save([&](const auto& value) { return saved.Write(value); }) &&
                restored.Load([&](auto& value) { return saved.Read(value); }) && restored.Summary() == state.Summary() &&
                restored.UsesThrottling() == throttled && restored.Capacity() == limit && saved.cursor == saved.data.size() &&
                !restored.Reserve(2), "v4 complete saves preserve the snapshotted throttle mode and capacity without replay");
        }
    }
    {
        State state(1327, SchedulingPolicy::ContinuationsFirst, false);
        std::vector<Token> pending;
        std::vector<unsigned> phases(1327);
        bool ordered = true;
        unsigned slices{};
        while (!state.done && slices < 6000) {
            if (!pending.empty()) {
                const auto token = pending.back();
                pending.pop_back();
                ordered &= state.Complete(token);
                ++phases[token.row];
            }
            const auto slice = RunSlice(state, [] { return 0ULL; }, [] { return 0ULL; }, [&](const Token& token) {
                ordered &= phases[token.row] == static_cast<unsigned>(token.phase);
                if (token.row >= 34 && (token.phase == Phase::WorkshopPlaced || token.phase == Phase::WorkshopMoved)) {
                    ordered &= state.Omit(token);
                    ++phases[token.row];
                } else {
                    ordered &= state.Accept(token);
                    if (token.row < 34) { pending.push_back(token); }
                    else { ordered &= state.Complete(token); ++phases[token.row]; }
                }
            });
            ordered &= slice.stop != SliceStop::StepLimit && slice.stop != SliceStop::TimeBudget;
            ++slices;
        }
        check(ordered && state.done && state.Valid() && state.peakOutstanding > 4 && state.returned == 2722 &&
            state.omittedCalls == 2586 && state.completedRows == 1327 && state.Summary()[0] == 1,
            "Off drains ready work without batching limits and retains exact no-op guards and real-return accounting");
    }
    {
        State state(100, SchedulingPolicy::ContinuationsFirst, false);
        std::uint64_t now{};
        const auto slice = RunSlice(state, [&] { return now; }, [] { return 0ULL; }, [&](const Token& token) {
            (void)state.Accept(token); now += 600000;
        });
        check(slice.steps == 100 && slice.stop == SliceStop::Capacity && state.outstanding == 100 && !state.done,
            "Off bypasses both the 64-step and 1ms budget, then yields only for actual pending callbacks");
    }
    // Independent legacy layout: do not use the current writer to manufacture
    // the bytes that prove old four-slot records and their trailing IDs load.
    for (const auto version : { 1u, 2u, 3u }) {
        for (const bool throttled : { true, false }) {
            if (version < 3 && !throttled) { continue; }
            Bytes legacy;
            if (version == 3) { legacy.Write(throttled); }
            for (const std::uint32_t value : { 12u, 48u, 4u, 0u, 0u, 4u, 0u, 0u }) { legacy.Write(value); }
            legacy.Write(std::uint64_t{10});
            legacy.Write(std::uint32_t{4}); legacy.Write(std::uint32_t{0}); legacy.Write(std::uint64_t{5});
            for (int i = 0; i < 4; ++i) { legacy.Write(false); }
            for (std::uint32_t row = 0; row < 12; ++row) {
                legacy.Write(std::uint8_t{0}); legacy.Write(false); legacy.Write(false); legacy.Write(row < 4);
            }
            const auto capacity = throttled ? 4u : 12u;
            for (std::uint32_t slot = 0; slot < capacity; ++slot) {
                const bool active = slot < 4;
                legacy.Write(active); legacy.Write(active); legacy.Write(active ? slot : 0u); legacy.Write(std::uint8_t{0});
                legacy.Write(active ? slot : 0u); legacy.Write(active ? std::uint64_t{slot + 1} : 0ULL);
                legacy.Write(active ? 10ULL : 0ULL);
            }
            if (version >= 2) { for (int row = 0; row < 12; ++row) { legacy.Write(std::uint8_t{0}); } }
            const auto stateEnd = legacy.data.size();
            for (std::uint32_t slot = 0; slot < capacity; ++slot) {
                legacy.Write(100u + slot); legacy.Write(200u + slot);
            }
            State restored;
            check(restored.Load([&](auto& value) { return legacy.Read(value); }, version) && legacy.cursor == stateEnd &&
                restored.Capacity() == capacity && restored.outstanding == 4 && restored.returned == 0 &&
                restored.interrupted && restored.done && restored.Valid() && !restored.Reserve(20),
                "independent v1/v2/v3 bytes retain original slot width and interrupt pending calls without replay");
            bool idsMatch = true;
            for (std::uint32_t slot = 0; slot < restored.Capacity(); ++slot) {
                std::uint32_t target{}, argument{};
                idsMatch &= legacy.Read(target) && legacy.Read(argument) && target == 100u + slot && argument == 200u + slot;
            }
            check(idsMatch && legacy.cursor == legacy.data.size(), "legacy dispatch diagnostic IDs stay aligned after the state payload");
            Bytes migrated;
            State reread;
            check(restored.Save([&](const auto& value) { return migrated.Write(value); }) &&
                reread.Load([&](auto& value) { return migrated.Read(value); }) && reread.Capacity() == capacity &&
                reread.Summary() == restored.Summary() && !reread.Reserve(21) && migrated.cursor == migrated.data.size(),
                "v4 re-save preserves legacy capacity, uncertain ownership and interruption");
        }
    }
    {
        State eight(12, SchedulingPolicy::ContinuationsFirst, true, kMaximumOutstanding);
        std::vector<Token> held;
        while (const auto token = eight.Reserve(10)) { (void)eight.Accept(*token); held.push_back(*token); }
        check(held.size() == 8 && eight.returned == 0 && eight.Summary()[0] == 0 && !eight.Reserve(11),
            "eight pending calls block a ninth and the final completion barrier remains closed");
        for (const auto version : { 1u, 2u, 3u }) {
            Bytes old;
            check(!eight.Save([&](const auto& value) { return old.Write(value); }, version) && old.data.empty(),
                "eight-slot state cannot be mislabeled as a four-slot predecessor payload");
        }
        Bytes good;
        (void)eight.Save([&](const auto& value) { return good.Write(value); }, 4);
        {
            auto previousBuild = good;
            const auto stateEnd = previousBuild.data.size();
            for (std::uint32_t slot = 0; slot < 8; ++slot) {
                previousBuild.Write(100u + slot); previousBuild.Write(200u + slot);
            }
            State restored(12);
            check(restored.Capacity() == 4 && restored.Load([&](auto& value) { return previousBuild.Read(value); }, 4) &&
                restored.Capacity() == 8 && previousBuild.cursor == stateEnd && restored.outstanding == 8 &&
                restored.interrupted && restored.done && restored.Valid() && !restored.Reserve(20) &&
                !restored.Complete(held.front()),
                "four-call default still reads the previous eight-slot v4 job and interrupts without replay");
            bool idsMatch = true;
            for (std::uint32_t slot = 0; slot < restored.Capacity(); ++slot) {
                std::uint32_t target{}, argument{};
                idsMatch &= previousBuild.Read(target) && previousBuild.Read(argument) && target == 100u + slot && argument == 200u + slot;
            }
            check(idsMatch && previousBuild.cursor == previousBuild.data.size(),
                "eight-slot v4 dispatch diagnostic IDs remain aligned after reverting new-job capacity");
            Bytes resaved;
            State reread;
            check(restored.Save([&](const auto& value) { return resaved.Write(value); }) &&
                reread.Load([&](auto& value) { return resaved.Read(value); }) && reread.Capacity() == 8 &&
                reread.Summary() == restored.Summary() && resaved.cursor == resaved.data.size() && !reread.Reserve(21),
                "resaving an interrupted eight-slot job preserves its capacity and result");
        }
        for (const auto capacity : { 0u, 3u, 4u, 9u, kMaximumRows + 1 }) {
            auto corrupt = good;
            std::memcpy(corrupt.data.data() + corrupt.fields[1], &capacity, sizeof(capacity));
            State prior(7);
            check(!prior.Load([&](auto& value) { return corrupt.Read(value); }, 4) && prior.rows.size() == 7 && !prior.done,
                "invalid or inconsistent v4 capacity cannot replace the live job");
        }
    }
    {
        State eight(100, SchedulingPolicy::ContinuationsFirst, true, kMaximumOutstanding);
        std::uint64_t now{};
        const auto slice = RunSlice(eight, [&] { return now; }, [] { return 0ULL; }, [&](const Token& token) {
            (void)eight.Accept(token); now += 150000;
        });
        check(slice.stop == SliceStop::TimeBudget && slice.steps == 7 && eight.outstanding == 7 &&
            slice.elapsedNs == 1050000 && !eight.done && eight.Valid(),
            "the unchanged 1ms budget stops an eight-slot batch before admitting its eighth expensive call");
    }
    for (const auto version : { 1u, 2u, 3u, 4u }) {
        for (const bool throttled : { true, false }) {
            if (version < 3 && !throttled) { continue; }
            State state(8, SchedulingPolicy::ContinuationsFirst, throttled, version < 4 ? kLegacyOutstanding : kMaximumOutstanding);
            std::vector<Token> pending;
            while (const auto token = state.Reserve(10)) { (void)state.Accept(*token); pending.push_back(*token); }
            Bytes saved;
            State restored;
            check(state.Save([&](const auto& value) { return saved.Write(value); }, version) &&
                restored.Load([&](auto& value) { return saved.Read(value); }, version) && saved.cursor == saved.data.size() &&
                restored.interrupted && restored.done && restored.Valid() && restored.Summary()[0] == -1 &&
                restored.UsesThrottling() == throttled && restored.outstanding == pending.size() &&
                !restored.Reserve(11) && !restored.Complete(pending.front()),
                "v1/v2 fixed-four, v3 On/Off and v4 capacity-aware pending loads retain uncertain ownership without replay");
        }
    }
    {
        State state(8, SchedulingPolicy::ContinuationsFirst, false);
        std::vector<Token> pending;
        while (const auto token = state.Reserve(100)) { (void)state.Accept(*token); pending.push_back(*token); }
        check(state.Complete(pending.back(), true) && state.failed == 1 && state.returned == 0 && state.outstanding == 7 &&
            !state.Complete(pending.back()) && state.Valid(), "Off counts a cancelled callback as one failure, never as a return");
        state.Cancel();
        check(!state.Reserve(101) && !state.Complete(pending.front()) && state.outstanding == 7 &&
            state.Summary()[0] == -1 && state.Valid(), "Off cancellation stops new calls and preserves pending callback ownership");
        Bytes old;
        check(!state.Save([&](const auto& value) { return old.Write(value); }, 2) && old.data.empty(),
            "Off jobs cannot be silently written into a predecessor fixed-four payload");
    }
    {
        State state(6, SchedulingPolicy::ContinuationsFirst, false);
        const auto rejected = state.Reserve(100).value();
        check(state.Reject(rejected) && state.failed == 1 && state.dispatched == 0 && !state.Accept(rejected),
            "Off dispatch rejection retires its row without creating later phases");
        std::vector<Token> pending;
        while (const auto token = state.Reserve(100)) { (void)state.Accept(*token); pending.push_back(*token); }
        state.Tick(kCallTimeoutMs + 99);
        check(!state.done && state.outstanding == 5, "Off callback deadline does not expire before five minutes");
        state.Tick(kCallTimeoutMs + 100);
        check(state.done && state.timedOut && state.Summary()[0] == 0 && state.Valid() && state.outstanding == 5 &&
            !state.Complete(pending.back()), "Off timeouts stop the job with uncertain calls and never bypass wiring's completion gate");
    }
    {
        State state(2, SchedulingPolicy::ContinuationsFirst, false);
        const auto first = state.Reserve(100).value();
        (void)state.Accept(first);
        (void)state.Complete(first);
        const auto next = state.Reserve(200).value();
        (void)state.Accept(next);
        state.Tick(kCallTimeoutMs + 100); // A retired slot's earlier cached deadline is harmless.
        check(!state.done && state.outstanding == 1, "retiring the earliest call cannot time out a newer callback");
        state.Tick(kCallTimeoutMs + 200);
        check(state.timedOut, "deadline cache recomputes the next live callback's exact deadline");
    }
    {
        State state(6, SchedulingPolicy::ContinuationsFirst, false);
        while (const auto token = state.Reserve(10)) { (void)state.Accept(*token); }
        Bytes saved;
        check(state.Save([&](const auto& value) { return saved.Write(value); }), "make an Off payload corruption fixture");
        const auto rejects = [&](Bytes corrupt) {
            State prior(7);
            return !prior.Load([&](auto& value) { return corrupt.Read(value); }) && prior.rows.size() == 7 && !prior.done;
        };
        for (const auto value : { std::byte{2}, std::byte{255} }) {
            auto corrupt = saved;
            corrupt.data.front() = value;
            check(rejects(corrupt), "malformed v3 throttle Boolean is rejected before replacing a live state");
        }
        auto corrupt = saved;
        const auto tooMany = kMaximumRows + 1;
        std::memcpy(corrupt.data.data() + corrupt.fields[2], &tooMany, sizeof(tooMany));
        check(rejects(corrupt), "v4 row count is bounded before row or slot allocation");
        const auto firstSlot = 18 + state.rows.size() * 4;
        corrupt = saved;
        const std::uint64_t duplicateGeneration = 1;
        std::memcpy(corrupt.data.data() + corrupt.fields[firstSlot + 7 + 5], &duplicateGeneration, sizeof(duplicateGeneration));
        check(rejects(corrupt), "linear validation rejects duplicated active generations in Off slots");
        corrupt = saved;
        const std::uint32_t wrongSlot = 0;
        std::memcpy(corrupt.data.data() + corrupt.fields[firstSlot + 7 + 4], &wrongSlot, sizeof(wrongSlot));
        check(rejects(corrupt), "Off slot identifiers must match both their storage and physical row");
        corrupt = saved;
        corrupt.data.pop_back();
        check(rejects(corrupt), "truncated variable-capacity payload is rejected without replacing live state");
        for (const auto version : { 0u, kCurrentVersion + 1 }) {
            State prior;
            auto bytes = saved;
            check(!prior.Load([&](auto& value) { return bytes.Read(value); }, version) && bytes.cursor == 0,
                "unsupported workshop payload versions are rejected without consuming input");
        }
    }
    for (const bool throttled : { true, false }) {
        State state(3, SchedulingPolicy::ContinuationsFirst, throttled);
        const auto first = state.Reserve(100).value();
        (void)state.Accept(first);
        const auto olderPending = state.Reserve(150).value();
        (void)state.Accept(olderPending);
        (void)state.Complete(first);
        const auto reused = state.Reserve(200).value();
        (void)state.Accept(reused);
        state.Tick(kCallTimeoutMs + 100);
        check(!state.done && state.outstanding == 2, "reusing an expired slot keeps older pending callbacks' deadlines distinct");
        state.Tick(kCallTimeoutMs + 149);
        check(!state.done, "another older callback is not timed out before its exact deadline");
        state.Tick(kCallTimeoutMs + 150);
        check(state.timedOut && state.outstanding == 2 && !state.Complete(olderPending) && !state.Complete(reused) && state.Valid(),
            "deadline cache expires the older pending call even when its earlier peer was completed and reused");
    }
    {
        State state(kMaximumRows, SchedulingPolicy::ContinuationsFirst, false);
        std::uint32_t admitted{};
        while (const auto token = state.Reserve(0)) { (void)state.Accept(*token); ++admitted; }
        bool blocked = true;
        for (std::uint64_t now = 1; now < 10000; ++now) { blocked &= !state.Reserve(now); }
        check(blocked && admitted == kMaximumRows && state.outstanding == kMaximumRows && state.Valid() && !state.done,
            "maximum-size Off jobs fill bounded row-index storage and repeated capacity polls do not rescan all pending slots");
    }
    {
        State state(5134, SchedulingPolicy::ContinuationsFirst, false);
        std::deque<Token> pending;
        std::vector<unsigned> phases(5134);
        std::uint64_t now{};
        bool ordered = true;
        while (const auto token = state.Reserve(now)) { (void)state.Accept(*token); pending.push_back(*token); }
        while (!pending.empty() && !state.done) {
            now += 100; // Older queued calls exceed five minutes; real work keeps returning.
            state.Tick(now);
            const auto token = pending.front(); pending.pop_front();
            ordered &= static_cast<unsigned>(token.phase) == phases[token.row]++ && state.Complete(token);
            while (const auto next = state.Reserve(now)) { (void)state.Accept(*next); pending.push_back(*next); }
        }
        check(ordered && now > kCallTimeoutMs && state.done && !state.timedOut && state.returned == 20536 &&
            state.completedRows == 5134 && state.outstanding == 0 && state.Valid() && state.Summary()[0] == 1,
            "5134-row Off queue may drain beyond five minutes while every ordered callback must actually return");
    }
    for (const bool throttled : { true, false }) {
        State state(3, SchedulingPolicy::ContinuationsFirst, throttled);
        const auto held = state.Reserve(0).value(); (void)state.Accept(held);
        const auto returned = state.Reserve(0).value(); (void)state.Accept(returned);
        state.Tick(60000);
        (void)state.Complete(returned);
        state.Tick(kCallTimeoutMs);
        check(state.timedOut == throttled, "On retains per-call deadlines; Off permits old queued calls while real returns make progress");
        if (!throttled) {
            const auto omitted = state.Reserve(kCallTimeoutMs + 10000).value();
            (void)state.Reject(omitted); // Dispatch failures must not extend the watchdog.
            state.Tick(kCallTimeoutMs + 59999);
            check(!state.done && state.outstanding == 1, "Off waits the full five minutes since its last successful return");
            state.Tick(kCallTimeoutMs + 60000);
            check(state.done && state.timedOut && state.outstanding == 1 && state.Summary()[0] == 0 && state.Valid(),
                "a stalled Off queue still stops incomplete; rejection cannot conceal a hung callback");
        }
    }
    {
        State state(100, SchedulingPolicy::ContinuationsFirst, false);
        ProgressPulse pulse;
        std::uint64_t now{};
        unsigned notices{};
        notices += pulse.Due(0, false);
        const auto slice = RunSlice(state, [&] { return now; }, [&] { return now / 1000000; }, [&](const Token& token) {
            notices += pulse.Due(now / 1000000, state.done);
            (void)state.Accept(token); (void)state.Complete(token);
            now += 100000000;
        });
        check(slice.steps == 400 && slice.stop == SliceStop::Complete && state.Summary()[0] == 1 && notices == 4 &&
            !pulse.Due(now / 1000000, true), "unrestricted long dispatch refreshes the notice every ten seconds and stops on completion");
    }
    {
        State state(3, SchedulingPolicy::ContinuationsFirst, false);
        const auto first = state.Reserve(0).value(); (void)state.Accept(first);
        const auto held = state.Reserve(0).value(); (void)state.Accept(held);
        CompletionSignal signal;
        signal.Resolve(Completion::Returned); // VM finished while a long dispatch pass ran.
        check(!state.TimeoutCheckDue(kCallTimeoutMs - 1) && state.TimeoutCheckDue(kCallTimeoutMs),
            "long Off passes poll other completion signals before evaluating an expired watchdog");
        state.elapsedMs = kCallTimeoutMs;
        if (signal.Get() == Completion::Returned) { (void)state.Complete(first); }
        const auto continuation = state.Reserve(kCallTimeoutMs);
        check(continuation && !state.timedOut && state.returned == 1 && state.outstanding == 2 &&
            !state.TimeoutCheckDue(kCallTimeoutMs + 1), "a real return published before timeout keeps a draining queue alive before its next dispatch");
    }
}
