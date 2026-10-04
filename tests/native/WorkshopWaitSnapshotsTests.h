// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorkshopWaitSnapshots.h"

template<class Check> void CheckWorkshopWaitSnapshots(Check&& check)
{
    using namespace Clipboard::WorkshopCallbacks;
    const auto pending = [](std::uint32_t) { return true; };
    {
        check(UseEarlyWaitSnapshot(true, true, true, true, 0), "early snapshot admits reviewed On/single/zero-spacing diagnostic");
        check(!UseEarlyWaitSnapshot(false, true, true, true, 0) &&
              !UseEarlyWaitSnapshot(true, false, true, true, 0), "both diagnostic options are required for early snapshot");
        check(!UseEarlyWaitSnapshot(true, true, false, true, 0) &&
              !UseEarlyWaitSnapshot(true, true, true, false, 0) &&
              !UseEarlyWaitSnapshot(true, true, true, true, 100),
            "early snapshot leaves Off, normal admission and spacing comparison untouched");
        State state(1);
        const auto token = *state.Reserve(0);
        (void)state.Accept(token);
        const auto before = state.Summary();
        WaitSnapshotSchedule plan(true);
        check(plan.EarlyEnabled() && plan.Attempts() == 0 &&
              !plan.Poll(state, 0, true, pending) && !plan.Poll(state, 4000, true, pending),
            "early observation is armed without sampling a young call");
        const auto early = plan.Poll(state, 5000, true, pending);
        check(early && early->thresholdMs == 5000 && early->oldest == token &&
              early->ageMs == 5000 && early->ordinal == 1 && early->skippedThresholds == 0,
            "five-second pending call yields one identified early snapshot");
        check(state.Summary() == before && state.Matches(token) && plan.Attempts() == 1,
            "early snapshot does not acknowledge a return or change ownership or watchdog state");
        check(!plan.Poll(state, 5000, true, pending) && !plan.Poll(state, 13000, true, pending),
            "slow short call does not repeatedly dump after first early observation");
        (void)state.Complete(token);
        const auto replacement = *state.Reserve(15000);
        (void)state.Accept(replacement);
        check(!plan.Poll(state, 20000, true, pending), "replacement request cannot rearm five-second allowance");
        const auto thirty = plan.Poll(state, 45000, true, pending);
        const auto sixty = plan.Poll(state, 75000, true, pending);
        const auto ninety = plan.Poll(state, 105000, true, pending);
        check(thirty && thirty->oldest == replacement && thirty->thresholdMs == 30000 && thirty->ordinal == 2 &&
              sixty && sixty->thresholdMs == 60000 && sixty->ordinal == 3 &&
              ninety && ninety->thresholdMs == 90000 && ninety->ordinal == 4 && plan.Attempts() == 4,
            "original pending-age thresholds remain available after early observation across slot reuse");
        check(!plan.Poll(state, kCallTimeoutMs + 14999, true, pending), "early mode is capped at four observations per job");
        state.Tick(kCallTimeoutMs + 14999);
        check(!state.done, "early observation does not shorten accepted-call watchdog");
        state.Tick(kCallTimeoutMs + 15000);
        check(state.timedOut && state.outstanding == 1 && !plan.Poll(state, kCallTimeoutMs + 15000, true, pending),
            "five-minute watchdog ends job independently of spent observations");
    }
    {
        State state(1);
        const auto token = *state.Reserve(0);
        (void)state.Accept(token);
        WaitSnapshotSchedule plan(true);
        check(!plan.Poll(state, 5000, false, pending) && plan.Attempts() == 0,
            "logging/restored-job gate suppresses early observation without consuming it");
        const auto late = plan.Poll(state, 35000, true, pending);
        check(late && late->thresholdMs == 30000 && late->skippedThresholds == 1 &&
              late->ordinal == 1 && !plan.Poll(state, 36000, true, pending),
            "late early-mode poll coalesces five and thirty seconds into a single dump");
        state.Cancel();
        check(!plan.Poll(state, 95000, true, pending) && plan.Attempts() == 1,
            "cancelled early-mode job emits no later snapshots");
    }
    {
        State state(1);
        const auto token = *state.Reserve(0);
        WaitSnapshotSchedule plan(true);
        check(!plan.Poll(state, 5000, true, pending), "early snapshot ignores unaccepted reservation");
        (void)state.Accept(token);
        const auto terminalSignal = [](std::uint32_t) { return false; };
        check(!plan.Poll(state, 6000, true, terminalSignal) && plan.Attempts() == 0,
            "returned or cancelled signal cannot trigger early snapshot before scheduler consumption");
        (void)state.Complete(token);
        const auto next = *state.Reserve(7000);
        (void)state.Accept(next);
        const auto early = plan.Poll(state, 12000, true, pending);
        check(early && early->oldest == next && early->oldest.generation != token.generation && early->thresholdMs == 5000,
            "unspent early observation identifies the actual later pending generation");
    }
    {
        State state(4);
        const auto a = *state.Reserve(0);
        const auto b = *state.Reserve(1000);
        (void)state.Accept(a);
        (void)state.Accept(b);
        const auto before = state.Summary();
        WaitSnapshotSchedule plan;
        check(!plan.Poll(state, 0, true, pending) && !plan.Poll(state, 29000, true, pending),
            "wait snapshots do not run for young callbacks");
        const auto first = plan.Poll(state, 30000, true, pending);
        check(first && first->ordinal == 1 && first->thresholdMs == 30000 && first->oldest == a && first->ageMs == 30000,
            "first observation selects oldest accepted request and preserves generation");
        check(!plan.Poll(state, 30000, true, pending) && !plan.Poll(state, 59000, true, pending),
            "no duplicate observation before next pending-age threshold");
        const auto second = plan.Poll(state, 60000, true, pending);
        const auto third = plan.Poll(state, 90000, true, pending);
        check(second && second->ordinal == 2 && second->thresholdMs == 60000 && third && third->ordinal == 3 && third->thresholdMs == 90000,
            "same pending callback is observed at sixty and ninety seconds");
        check(!plan.Poll(state, 119000, true, pending), "pre-timeout observations are capped at three per job");
        check(state.Summary() == before && state.Matches(a) && state.Matches(b),
            "observations do not advance rows, count returns or alter callback ownership");
        state.Tick(kCallTimeoutMs);
        check(state.timedOut && state.outstanding == 2 && !plan.Poll(state, kCallTimeoutMs, true, pending),
            "original timeout still fires and suppresses pre-timeout observations");
    }
    {
        State state(1);
        const auto firstToken = *state.Reserve(0);
        (void)state.Accept(firstToken);
        WaitSnapshotSchedule plan;
        (void)plan.Poll(state, 30000, true, pending);
        (void)state.Complete(firstToken);
        const auto replacement = *state.Reserve(35000);
        (void)state.Accept(replacement);
        check(!plan.Poll(state, 65000, true, pending), "slot reuse does not reset spent threshold allowance");
        const auto observed = plan.Poll(state, 95000, true, pending);
        check(observed && observed->oldest == replacement && observed->oldest.generation != firstToken.generation && observed->ordinal == 2,
            "replacement request is identified without conflating its age with former occupant");
    }
    {
        State state(2);
        const auto oldest = *state.Reserve(0);
        const auto newer = *state.Reserve(10000);
        (void)state.Accept(oldest);
        (void)state.Accept(newer);
        WaitSnapshotSchedule plan;
        const auto newerStillPending = [&](std::uint32_t slot) { return slot == newer.slot; };
        check(!plan.Poll(state, 30000, true, newerStillPending),
            "returned or cancelled signal is excluded even before scheduler consumes it");
        const auto observed = plan.Poll(state, 40000, true, newerStillPending);
        check(observed && observed->oldest == newer && observed->ageMs == 30000 && state.returned == 0,
            "signal observation is not callback acknowledgement");
    }
    {
        State state(1);
        const auto token = *state.Reserve(0);
        WaitSnapshotSchedule plan;
        check(!plan.Poll(state, 30000, true, pending), "reserved but unaccepted work is not sampled");
        (void)state.Accept(token);
        check(!plan.Poll(state, 60000, false, pending), "disabled logging/profile or restored-job gate consumes no threshold");
        const auto late = plan.Poll(state, 95000, true, pending);
        check(late && late->ordinal == 1 && late->thresholdMs == 90000 && late->skippedThresholds == 2,
            "late polling takes one current observation and labels skipped thresholds");
        check(!plan.Poll(state, 96000, true, pending), "late polling cannot emit catch-up dumps");
    }
    {
        State state(1);
        const auto token = *state.Reserve(0);
        (void)state.Accept(token);
        WaitSnapshotSchedule plan;
        unsigned reads{};
        auto countReads = [&](std::uint32_t) { ++reads; return true; };
        (void)plan.Poll(state, 29000, true, countReads);
        (void)plan.Poll(state, 29999, true, countReads);
        (void)plan.Poll(state, 1, true, countReads);
        check(reads == 1, "slot scans are limited to once a second and reject backward time");
        check(plan.Poll(state, 30000, true, countReads).has_value(), "observation proceeds on next eligible poll");
        state.Cancel();
        check(!plan.Poll(state, 90000, true, countReads) && reads == 2,
            "cancelled jobs neither scan nor dump");
    }
    {
        State state(1, SchedulingPolicy::ContinuationsFirst, false);
        const auto token = *state.Reserve(0);
        (void)state.Accept(token);
        WaitSnapshotSchedule plan;
        check(plan.Poll(state, 30000, true, pending).has_value(), "unthrottled jobs can be observed without changing their policy");
        state.Tick(60000);
        (void)state.Complete(token);
        const auto next = *state.Reserve(60000);
        (void)state.Accept(next);
        check(!plan.Poll(state, 90000, true, pending), "Off uses current request age for diagnostics, not time since job start");
        state.Tick(kCallTimeoutMs + 59999);
        check(!state.done, "diagnostics do not shorten Off's progress watchdog");
        state.Tick(kCallTimeoutMs + 60000);
        check(state.timedOut, "Off's existing no-return watchdog is unchanged");
    }
    {
        State empty;
        WaitSnapshotSchedule plan;
        check(!plan.Poll(empty, 90000, true, pending), "empty completed jobs produce no wait snapshots");
    }
}
