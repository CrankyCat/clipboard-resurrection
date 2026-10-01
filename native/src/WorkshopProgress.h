// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include "WorkshopCallbackState.h"
#include "WorkshopCallbackSlice.h"
#include <chrono>
#include <mutex>

namespace Clipboard::WorkshopCallbacks
{
    [[nodiscard]] inline std::uint64_t ClockNs() noexcept
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    class ProgressPulse
    {
    public:
        [[nodiscard]] bool Due(std::uint64_t nowMs, bool done) noexcept
        {
            if (done || (started && (nowMs < lastMs || nowMs - lastMs < kProgressIntervalMs))) { return false; }
            started = true;
            lastMs = nowMs; // one pulse after a long stall, never a catch-up burst
            return true;
        }
    private:
        std::uint64_t lastMs{};
        bool started{};
    };

    // F4SE may run a queued HUD task after this serialized functor is gone.
    // It retains only this small gate and a weak import lease, never the job.
    class ProgressNotice
    {
    public:
        [[nodiscard]] bool TryQueue() noexcept
        {
            if (!active.load(std::memory_order_acquire)) { return false; }
            bool expected = false;
            return pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
        }
        [[nodiscard]] bool MayDisplay() const noexcept { return active.load(std::memory_order_acquire); }
        template<class Display> bool DisplayIfActive(Display&& display)
        {
            // Finish waits for any emission already in progress. Once Stop
            // returns, queued tasks cannot emit another continuation notice.
            std::scoped_lock lock(displayLock);
            if (!MayDisplay()) { return false; }
            display();
            return true;
        }
        [[nodiscard]] bool HasCompletedAttempt() const noexcept { return completedAttempt.load(std::memory_order_acquire); }
        [[nodiscard]] bool WaitForInitialAttempt(std::uint64_t elapsedMs) const noexcept
        {
            // Give the first engine task a chance to show the notice, without
            // hanging import if a paused/unavailable HUD queue does not run.
            return !HasCompletedAttempt() && elapsedMs < 1000;
        }
        void Complete() noexcept
        {
            completedAttempt.store(true, std::memory_order_release);
            pending.store(false, std::memory_order_release);
        }
        void Stop() noexcept
        {
            std::scoped_lock lock(displayLock);
            active.store(false, std::memory_order_release);
        }
    private:
        std::mutex displayLock;
        std::atomic_bool active{ true }, pending{ false };
        std::atomic_bool completedAttempt{ false };
    };
}
