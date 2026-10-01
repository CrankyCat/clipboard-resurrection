// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorkshopProgress.h"
#include "ImportPercentage.h"
#include "ImportProgressChannel.h"
#include <functional>
#include <memory>
#include <latch>
#include <thread>

template<class Check> void CheckWorkshopProgress(Check&& check)
{
    using namespace Clipboard::WorkshopCallbacks;
    {
        using namespace Clipboard::ImportProgress;
        using ProgressPhase = Clipboard::ImportProgress::Phase;
        DisplayChannel channel;
        const auto workshop = channel.Begin(100, ProgressPhase::Workshop);
        channel.Update(workshop, 0, 436);
        check(channel.TryQueue() && !channel.TryQueue(), "one UI refresh may be outstanding");
        channel.Update(workshop, 50, 436);
        channel.Update(workshop, 99, 436);
        DisplaySnapshot seen;
        check(channel.Deliver([&](const auto& value) { seen = value; return true; }) && seen.Percent() == 22 && seen.done == 99,
            "delayed UI task reads latest returned counts rather than its dispatch-time zero");
        channel.Update(workshop, 100, 436);
        check(!channel.TryQueue(), "same integer percent does not redisplay even as exact count updates");
        channel.Update(workshop, 200, 436);
        check(channel.TryQueue(), "new integer schedules a UI update");
        const auto power = channel.Begin(100, ProgressPhase::Power);
        channel.Update(power, 30, 100);
        channel.Update(workshop, 436, 436);
        channel.Finish(workshop);
        channel.Stop(workshop);
        check(channel.Deliver([&](const auto& value) { seen = value; return true; }) && seen.phase == ProgressPhase::Power && seen.Percent() == 30 && seen.visible,
            "queued workshop task and late workshop updates cannot overwrite or clear the power phase");
        channel.Update(power, 100, 100);
        check(channel.TryQueue() && channel.Deliver([&](const auto& value) { seen = value; return true; }) && seen.Percent() == 99,
            "callback totals alone cannot announce 100 before phase validation");
        channel.Finish(power);
        check(channel.TryQueue() && channel.Deliver([&](const auto& value) { seen = value; return true; }) && seen.Percent() == 100,
            "successful phase barrier updates the same line to 100");
        channel.Clear(101);
        check(!channel.TryQueue(), "another tool cannot remove the current tool's progress");
        channel.Clear(100);
        channel.Update(power, 50, 100);
        channel.Finish(power);
        check(channel.TryQueue() && channel.Deliver([&](const auto& value) { seen = value; return true; }) && !seen.visible,
            "cleanup suppresses late return and finish work and removes the line");
        const auto next = channel.Begin(100, ProgressPhase::Workshop);
        channel.Update(next, 1, 10);
        check(channel.TryQueue() && !channel.Deliver([](const auto&) { return false; }), "missing HUD is not acknowledged as rendered");
        channel.Update(next, 6, 10);
        check(channel.TryQueue() && channel.Deliver([&](const auto& value) { seen = value; return true; }) && seen.Percent() == 60,
            "HUD retry renders the current total, without replaying the failed ten percent");
        channel.Update(next, 7, 10);
        check(channel.TryQueue(), "changed progress schedules before reset");
        channel.Clear();
        check(channel.Deliver([&](const auto& value) { seen = value; return true; }) && !seen.visible,
            "load/reset invalidates a refresh already queued for the old game");
    }
    {
        using Clipboard::ImportProgress::RowUnits;
        RowUnits registrations(9684);
        for (std::size_t i = 0; i < 218; ++i) { registrations.Require(i, 3); }
        for (std::size_t i = 0; i < 50; ++i) { registrations.Complete(i, 1); }
        check(registrations.Total() == 436 && registrations.Done() == 50 && registrations.Percent() == 11,
            "placed registration callbacks advance before moved or object-handler callbacks run");
        registrations.Complete(49, 1);
        registrations.Complete(9000, 3);
        check(registrations.Done() == 50, "duplicate return and object-only row do not advance the workshop percentage");
        for (std::size_t i = 0; i < 50; ++i) { registrations.Complete(i, 2); }
        check(registrations.Done() == 100 && registrations.Percent() == 22,
            "movement callback earns the second half of each registration object's contribution");
    }
    {
        using Clipboard::ImportProgress::RowUnits;
        RowUnits registrations(9684);
        for (std::size_t i = 0; i < 218; ++i) { registrations.Require(i, 1); }
        check(registrations.Total() == 218 && registrations.Percent() == 0, "initialization denominator excludes rows with only fast object handlers");
        registrations.Complete(9000, 1);
        check(registrations.Done() == 0, "non-registration object completion does not inflate initialization progress");
        registrations.Complete(0, 1);
        registrations.Complete(1, 1);
        check(registrations.Percent() == 0, "percentage rounds down to a whole number");
        registrations.Complete(2, 1);
        check(registrations.Percent() == 1, "third of 218 completions advances to one percent");
        registrations.Complete(2, 1);
        check(registrations.Done() == 3, "duplicate callback observations do not advance percentage");
        for (std::size_t i = 3; i < 218; ++i) { registrations.Complete(i, 1); }
        check(registrations.AllDone() && registrations.Percent() == 99, "registration row completion reserves 100 for the entire callback barrier");
        registrations.Require(9000, 1);
        check(registrations.Total() == 219 && registrations.Done() == 219, "late conservative promotion preserves prior terminal completion");
        registrations.Omit(9000, 1);
        registrations.Omit(9000, 1);
        check(registrations.Total() == 218 && registrations.Done() == 218, "verified no-op reclassification removes the row once without underflow");
        RowUnits power(3);
        power.Require(0, 3);
        power.Require(2, 3);
        power.Complete(1, 3);
        power.Complete(0, 1);
        power.Complete(2, 1);
        check(power.Total() == 4 && power.Percent() == 50, "network pass accounts for half of ready-object work; excluded row has no units");
        power.Complete(0, 1);
        check(power.Percent() == 50, "connection retry cannot double-count a row");
        power.Complete(0, 2);
        check(power.Percent() == 75 && !power.AllDone(), "animation candidate remains pending after non-animated row finishes");
        power.Complete(2, 2);
        check(power.AllDone() && power.Percent() == 99, "powering up reserves final percentage for caller validation");
        RowUnits empty;
        check(empty.Percent() == 0 && empty.AllDone(), "zero eligible rows neither divide by zero nor fabricate a pre-barrier 100");
        empty.Complete(99, 3);
        empty.Require(99, 3);
        check(empty.Total() == 0, "out-of-range stale row cannot change progress");
    }
    {
        using Clipboard::ImportProgress::PowerUnits;
        PowerUnits split(4);
        for (auto row : { 0U, 2U, 3U }) { split.Require(row); }
        split.Require(0);
        split.Require(99);
        check(split.Total() == 6 && split.Percent() == 0, "power session begins at zero with only ready rows included");
        split.SelectNetworkPlan(true);
        split.SelectNetworkPlan(true);
        check(split.Total() == 9 && split.Done() == 0, "first assembly pass fixes three equal units per ready row exactly once");
        for (auto row : { 0U, 2U, 3U }) { split.Complete(row, PowerUnits::Assembly); }
        split.Complete(1, PowerUnits::Assembly);
        split.Complete(99, PowerUnits::Assembly);
        split.Complete(0, PowerUnits::Assembly);
        check(split.Done() == 3 && split.Percent() == 33 && !split.AllDone(), "assembly advances split progress to one third without credit for retries or excluded rows");
        split.SelectNetworkPlan(false); // the subsequent refresh must keep the split denominator
        for (auto row : { 0U, 2U, 3U }) { split.Complete(row, PowerUnits::Network); }
        check(split.Total() == 9 && split.Done() == 6 && split.Percent() == 66, "refresh earns the second third without changing the frozen split plan");
        split.Complete(0, PowerUnits::Network);
        split.Complete(0, PowerUnits::Animation); // native non-candidate completion
        split.Complete(2, PowerUnits::Animation); // Papyrus animation completion
        split.Complete(2, PowerUnits::Animation);
        check(split.Done() == 8 && !split.AllDone(), "unfinished or failed animation keeps split progress below completion");
        split.Complete(3, PowerUnits::Animation);
        check(split.AllDone() && split.Percent() == 99, "split 100 still requires the session completion barrier");

        PowerUnits combined(2);
        combined.Require(0);
        combined.Require(1);
        combined.SelectNetworkPlan(false);
        combined.SelectNetworkPlan(true); // a late caller cannot change the active plan
        combined.Require(0);
        for (auto row : { 0U, 1U }) { combined.Complete(row, PowerUnits::Network); }
        check(combined.Total() == 4 && combined.Percent() == 50, "combined mode retains its original half-network half-animation weighting");
        for (auto row : { 0U, 1U }) { combined.Complete(row, PowerUnits::Animation); }
        check(combined.AllDone() && combined.Percent() == 99, "combined completion remains gated");

        PowerUnits premature(1);
        premature.Require(0);
        premature.Complete(0, PowerUnits::Animation);
        premature.SelectNetworkPlan(true);
        check(premature.Total() == 2 && premature.Percent() == 50, "late plan selection cannot reduce an already credited percentage");
        PowerUnits missingAssembly(1);
        missingAssembly.Require(0);
        missingAssembly.SelectNetworkPlan(true);
        missingAssembly.Complete(0, PowerUnits::Network | PowerUnits::Animation);
        check(!missingAssembly.AllDone() && missingAssembly.Percent() == 66, "refresh and animation cannot conceal unfinished split assembly");
        PowerUnits empty;
        empty.SelectNetworkPlan(true);
        empty.Complete(0, PowerUnits::Assembly);
        check(empty.Total() == 0 && empty.Percent() == 0 && empty.AllDone(), "empty split plan has no synthetic work or division by zero");
    }
    {
        CompletionSignal signal;
        signal.Resolve(Completion::Pending);
        signal.Resolve(Completion::Returned);
        signal.Resolve(Completion::Cancelled);
        check(signal.Get() == Completion::Returned, "first terminal notification wins over duplicates");
    }
    {
        ProgressPulse pulse;
        check(pulse.Due(0, false) && !pulse.Due(0, false) && !pulse.Due(9999, false), "initial notice is requested immediately, without a duplicate burst");
        check(pulse.Due(10000, false) && !pulse.Due(10000, false), "continuation emits once at ten-second interval boundary");
        check(!pulse.Due(19999, false) && pulse.Due(20000, false), "continuation repeats in later processing slices");
        check(pulse.Due(55000, false) && !pulse.Due(55001, false), "long scheduler delay produces one reminder instead of a burst");
        check(!pulse.Due(65000, true) && !pulse.Due(1, false), "completed and restored-time polls cannot send a continuation");
    }
    {
        ProgressPulse completed;
        check(!completed.Due(0, true) && !completed.Due(10000, true), "an empty or restored terminal job does not start a notice");
        ProgressNotice notice;
        check(notice.WaitForInitialAttempt(0) && notice.WaitForInitialAttempt(999) && !notice.WaitForInitialAttempt(1000),
            "first HUD dispatch is awaited briefly, but an unprocessed UI queue cannot hang import");
        check(notice.TryQueue() && notice.WaitForInitialAttempt(1), "queuing alone is not a completed initial HUD attempt");
        notice.Complete();
        check(notice.HasCompletedAttempt() && !notice.WaitForInitialAttempt(1), "actual HUD task completion releases the initial dispatch wait");
    }
    {
        ProgressNotice notice;
        std::latch localizing{ 1 }, localized{ 1 };
        unsigned displayed{};
        std::thread task([&] {
            localizing.count_down();
            localized.wait(); // localization can block before final eligibility check
            notice.DisplayIfActive([&] { ++displayed; });
        });
        localizing.wait();
        notice.Stop();
        localized.count_down();
        task.join();
        check(displayed == 0, "completion while localization is pending suppresses stale continuation");
    }
    {
        ProgressNotice notice;
        std::latch emitting{ 1 }, release{ 1 }, stopping{ 1 };
        bool displayed{}, finishObservedDisplay{};
        std::thread task([&] {
            notice.DisplayIfActive([&] { emitting.count_down(); release.wait(); displayed = true; });
        });
        emitting.wait();
        std::thread finish([&] { stopping.count_down(); notice.Stop(); finishObservedDisplay = displayed; });
        stopping.wait();
        release.count_down();
        task.join();
        finish.join();
        check(finishObservedDisplay && !notice.DisplayIfActive([] {}), "Stop serializes with in-progress emission and prevents any later display");
    }
    {
        auto notice = std::make_shared<ProgressNotice>();
        check(notice->TryQueue() && !notice->TryQueue(), "only one HUD notice can remain pending");
        unsigned displayed{};
        std::function<void()> queued = [notice, &displayed] {
            notice->DisplayIfActive([&] { ++displayed; });
            notice->Complete();
        };
        notice->Stop();
        notice.reset();
        queued();
        check(displayed == 0, "HUD task retained after job termination discards its notice");
        ProgressNotice ongoing;
        check(ongoing.TryQueue() && ongoing.MayDisplay(), "live job can display its continuation");
        check(ongoing.DisplayIfActive([&] { ++displayed; }) && displayed == 1, "live emission is guarded through the display call");
        ongoing.Complete();
        check(ongoing.TryQueue(), "displayed notice permits the next timed continuation");
        ongoing.Stop();
        ongoing.Complete();
        check(!ongoing.TryQueue() && !ongoing.MayDisplay(), "completion cannot reactivate a stopped notice");
        check(!ongoing.DisplayIfActive([&] { ++displayed; }) && displayed == 1, "terminal invalidation suppresses queued emissions");
    }
}
