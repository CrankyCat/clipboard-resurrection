// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "PerformanceBatchState.h"
#include "ScrapTargetsTests.h"
#include <array>
#include <functional>
#include <latch>
#include <memory>
#include <thread>

template<class Check> void CheckPerformanceBatches(Check&& check)
{
    using namespace Clipboard::PerformanceBatch;
    {
        SliceHandoff handoff;
        Progress consumed{ 4 }, worker{ 4 };
        check(!handoff.Consume() && handoff.TryQueue(), "idle coordinator queues one engine slice");
        std::latch entered{ 1 }, release{ 1 };
        std::thread engine([&] {
            // The queue is deliberately held mid-slice while the coordinator
            // is polled and saved, reproducing the two-worker boundary.
            worker.cursor = worker.dispatched = 1;
            entered.count_down();
            release.wait();
            worker.cursor = worker.dispatched = 2;
            handoff.Publish();
        });
        entered.wait();
        check(!handoff.TryQueue() && !handoff.Consume(), "pending engine work cannot overlap or expose partial results");
        std::vector<std::uint32_t> saved;
        consumed.Save([&](auto value) { saved.push_back(value); return true; });
        check(saved == std::vector<std::uint32_t>{4,0,0,0,0}, "save reads completed progress while engine work is pending");
        release.count_down();
        engine.join();
        check(!handoff.TryQueue(), "unconsumed completion prevents another engine slice");
        check(handoff.Consume(), "coordinator acquires completed engine results");
        consumed = worker;
        check(consumed.cursor == 2 && consumed.dispatched == 2 && !handoff.Consume(), "completion is consumed exactly once");
        check(handoff.TryQueue(), "next slice is admitted only after completion is consumed");
    }
    {
        struct Job { SliceHandoff handoff; std::atomic_bool cancelled{}; unsigned calls{}; };
        auto coordinator = std::make_shared<Job>();
        const std::weak_ptr<Job> weak = coordinator;
        check(coordinator->handoff.TryQueue(), "cancellation fixture queues an engine slice");
        std::function<void()> queued = [job = coordinator] {
            if (!job->cancelled.load()) { ++job->calls; }
            job->handoff.Publish();
        };
        coordinator->cancelled.store(true);
        coordinator.reset();
        check(!weak.expired(), "queued task retains state after the serialized owner is destroyed");
        queued();
        {
            const auto retained = weak.lock();
            check(retained && retained->calls == 0 && retained->handoff.Consume(), "reverted queued task completes without touching game objects");
        }
        queued = {};
        check(weak.expired(), "completed cancelled task releases the final retained state");
    }
    check(!SliceComplete(true, 0, 9000), "scrap makes progress even after an expensive snapshot");
    check(!SliceComplete(true, 1, 100), "cheap scrap may use the remaining admission slot");
    check(SliceComplete(true, 2, 100), "scrap burst stops at two even with unused time");
    check(SliceComplete(true, 1, 2000), "one expensive engine call exhausts scrap time budget");
    check(SliceComplete(false, 64, 100), "animation classification is row bounded");
    check(SliceComplete(false, 1, 2000), "animation classification is time bounded");
    check(kScrapDelayMs > 0, "scrap yields across ticks instead of a same-tick busy loop");
    Progress paced{ 1058 };
    std::vector<std::uint32_t> submitted;
    VisitSlice(paced, true, [&](auto row) { submitted.push_back(row); ++paced.dispatched; }, [] { return 100U; });
    check(paced.cursor == 2 && submitted == std::vector<std::uint32_t>{0, 1}, "first scrap slice visits only its admitted targets");
    VisitSlice(paced, true, [&](auto row) { submitted.push_back(row); ++paced.dispatched; }, [] { return 3000U; });
    check(paced.cursor == 3 && submitted.back() == 2, "expensive resumed slice advances once without repeating a target");
    {
        using namespace Clipboard::Scrapping::Tests;
        Reference owner{ 100 }, first{ 1 }, second{ 2 }, wire{ 3 };
        {
            TestBatch batch;
            batch.Add(&first, &owner); batch.Add(&second, &owner); batch.Add(&wire, &owner);
            Progress state{ 3 };
            auto visit = [&](auto row) {
                const auto& target = batch.Targets()[row];
                if (target.reference->deleted) { ++state.skipped; return; }
                if (target.reference.get() == &first) { wire.deleted = true; }
                ++state.dispatched;
            };
            VisitSlice(state, true, visit, [] { return 0U; });
            check(state.cursor == 2 && owner.liveHolds == 2 && wire.liveHolds == 2,
                "targets and one unique owner remain retained between destructive slices");
            VisitSlice(state, true, visit, [] { return 0U; });
            check(state.cursor == 3 && state.dispatched == 2 && state.skipped == 1,
                "next slice skips the wire already removed by an earlier endpoint");
        }
        check(owner.BalancedAt(1) && first.BalancedAt(1) && second.BalancedAt(1) && wire.BalancedAt(1),
            "completion releases retained targets and unique owner without a refcount imbalance");
    }

    std::vector<std::int32_t> pairs;
    std::uint32_t unavailable{};
    for (std::uint32_t i = 0; i < 1058; ++i) {
        Classify(i, i == 4 ? -1 : (i % 100 == 0 ? 3 : 0), pairs, unavailable);
    }
    check(unavailable == 1 && pairs.size() == 22, "large sparse scan compacts candidates without dropping unavailable rows");
    check(pairs[0] == 0 && pairs[20] == 1000 && pairs[21] == 3, "physical indices and capability bits remain ordered above 128 rows");
    pairs.clear();
    for (std::uint32_t i = 0; i < 1058; ++i) { Classify(i, 2, pairs, unavailable); }
    check(pairs.size() == 2116 && pairs.back() == 2, "dense animation results are not truncated at the Papyrus allocation limit");
    pairs.clear();
    Classify(0, 0, pairs, unavailable);
    check(pairs.empty(), "ordinary models produce a successful empty candidate list");

    Progress original{ 1058, 500, 450, 49, 1 };
    std::vector<std::uint32_t> payload;
    check(original.Save([&](auto value) { payload.push_back(value); return true; }), "partial destruction saves cursor and outcome counts");
    auto load = [&](const std::vector<std::uint32_t>& input, Progress& state) {
        std::size_t index{};
        return state.Load([&](auto& value) {
            if (index == input.size()) { return false; }
            value = input[index++]; return true;
        }, 1058);
    };
    Progress restored;
    check(load(payload, restored) && restored.interrupted && restored.cursor == 500 && restored.dispatched == 450,
        "load preserves partial progress and interrupts instead of replaying destruction");
    bool replayed{};
    VisitSlice(restored, true, [&](auto) { replayed = true; }, [] { return 0U; });
    check(!replayed && restored.cursor == 500, "the runtime slice driver refuses to replay an interrupted cursor");
    for (const auto bad : { std::vector<std::uint32_t>{1059,0,0,0,0}, {1058,1059,0,0,0},
        {1058,5,6,0,0}, {1058,5,1,1} }) {
        check(!load(bad, restored), "reject invalid or truncated saved progress");
    }
}
