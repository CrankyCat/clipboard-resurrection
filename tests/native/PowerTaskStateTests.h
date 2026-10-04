// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "PowerTaskState.h"
#include <functional>
#include <latch>
#include <memory>
#include <thread>

template<class Check> void CheckPowerTaskState(Check&& check)
{
    using namespace Clipboard::PowerTask;
    {
        Epoch epoch;
        Gate gate(epoch);
        struct Progress { unsigned calls{}; bool done{}; } saved, worker;
        const auto first = gate.TryQueue();
        check(first && gate.CanRun(first) && gate.Outstanding(), "power task admits its first slice");
        std::latch entered{ 1 }, release{ 1 };
        std::thread engine([&] {
            worker.calls = 1;
            entered.count_down();
            release.wait();
            worker.calls = 2;
            worker.done = true;
            gate.Publish(first);
        });
        entered.wait();
        check(!gate.TryQueue() && !gate.Consume(), "pending power task neither overlaps nor completes early");
        check(saved.calls == 0 && !saved.done, "a pending save uses the last consumed checkpoint");
        release.count_down();
        // Read worker data only after the gate's acquire publication, before
        // joining: visibility must come from the handoff rather than join.
        while (!gate.Consume()) { std::this_thread::yield(); }
        saved = worker;
        check(saved.calls == 2 && saved.done && !gate.Outstanding(), "consumption acquires a complete power result");
        engine.join();
        check(!gate.Consume(), "power completion cannot be consumed twice");
        const auto second = gate.TryQueue();
        check(second && second.value != first.value && gate.CanRun(second), "power rescheduling uses a new slice ticket");
        check(!gate.Publish(first) && !gate.FailSubmission(first), "late publication or failure cannot affect a newer slice");
        check(!gate.Consume() && gate.CanRun(second) && !gate.Failed(), "stale completion leaves the new task pending");
        check(gate.Publish(second) && !gate.TryQueue(), "ready power result blocks rescheduling until consumed");
        check(gate.Consume(), "the new power completion is consumed normally");
    }
    {
        Epoch epoch;
        struct Job {
            explicit Job(const Epoch& domain) : gate(domain) {}
            Gate gate;
            unsigned unpackCalls{};
            unsigned cleanupCalls{};
        };
        auto coordinator = std::make_shared<Job>(epoch);
        const auto ticket = coordinator->gate.TryQueue();
        const std::weak_ptr<Job> weak = coordinator;
        std::function<void()> queued = [job = coordinator, ticket] {
            if (job->gate.CanRun(ticket)) { ++job->unpackCalls; }
            ++job->cleanupCalls;
            job->gate.Publish(ticket);
        };
        coordinator->gate.Cancel();
        coordinator.reset();
        check(!weak.expired(), "queued power task retains shared state after coordinator destruction");
        queued();
        {
            const auto retained = weak.lock();
            check(retained && retained->unpackCalls == 0 && retained->cleanupCalls == 1,
                "canceled queued task performs cleanup without unpacking VM references");
            check(retained && retained->gate.Consume() && !retained->gate.TryQueue(),
                "canceled power completion drains but cannot restart");
        }
        queued = {};
        check(weak.expired(), "completed canceled task releases its retained shared state");
    }
    {
        Epoch epoch;
        Gate beforeLoad(epoch);
        const auto ticket = beforeLoad.TryQueue();
        epoch.Invalidate();
        check(beforeLoad.Cancelled() && !beforeLoad.CanRun(ticket), "load invalidates a queued task before any VM access");
        check(!beforeLoad.Consume(), "load invalidation does not invent completion for a pending task");
        check(beforeLoad.Publish(ticket) && beforeLoad.Consume(), "invalidated task can publish its cleanup completion");
        check(!beforeLoad.TryQueue(), "pre-load task never replays after invalidation");
        Gate afterLoad(epoch);
        const auto next = afterLoad.TryQueue();
        check(next && afterLoad.CanRun(next) && !afterLoad.Cancelled(), "a new load generation accepts fresh work");
        afterLoad.Cancel();
        check(!afterLoad.CanRun(next), "cancellation between engine operations stops the remaining slice");
        check(afterLoad.Publish(next) && afterLoad.Consume(), "mid-slice cancellation still drains its completion");
    }
    {
        Epoch epoch;
        Gate rejected(epoch);
        const auto ticket = rejected.TryQueue();
        check(rejected.FailSubmission(ticket) && rejected.Failed(), "missing task interface fails submission closed");
        check(!rejected.CanRun(ticket) && !rejected.Publish(ticket), "rejected work cannot run or publish a fallback result");
        check(rejected.Outstanding() && !rejected.TryQueue(), "submission failure awaits coordinator consumption");
        check(rejected.Consume() && rejected.Failed() && !rejected.Outstanding(), "submission failure is acquired as terminal status");
        check(!rejected.Consume() && !rejected.TryQueue(), "failed submission is neither consumed twice nor retried");
    }
    {
        Epoch epoch;
        Gate neverQueued(epoch);
        neverQueued.Cancel();
        check(!neverQueued.TryQueue() && !neverQueued.Outstanding() && !neverQueued.Consume(),
            "cancellation before admission leaves no outstanding engine task");
        check(!neverQueued.CanRun({}) && !neverQueued.Publish({}) && !neverQueued.FailSubmission({}),
            "an empty ticket cannot run, complete or fail a job");
    }
}
