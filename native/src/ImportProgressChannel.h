// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once
#include <algorithm>
#include <cstdint>
#include <mutex>

namespace Clipboard::ImportProgress
{
    enum class Phase { Workshop, Power };
    struct DisplaySnapshot
    {
        std::uint64_t generation{}, revision{};
        std::uint32_t owner{}, done{}, total{};
        Phase phase{ Phase::Workshop };
        bool visible{}, complete{};
        [[nodiscard]] int Percent() const noexcept
        {
            return complete ? 100 : total ? static_cast<int>(std::min<std::uint64_t>(99, 100ULL * done / total)) : 0;
        }
    };

    // One latest-value mailbox for the HUD, not a queue of historical strings.
    // No game references or serialized state. The renderer runs under this lock
    // so completion/cancellation cannot be overtaken by an older UI task.
    class DisplayChannel
    {
    public:
        std::uint64_t Begin(std::uint32_t owner, Phase phase)
        {
            std::scoped_lock lock(mutex);
            const auto generation = state.generation + 1;
            const auto revision = state.revision + 1;
            state = { generation, revision, owner, 0, 0, phase, true, false };
            accepting = true;
            return generation;
        }
        void Update(std::uint64_t generation, std::uint32_t done, std::uint32_t total)
        {
            std::scoped_lock lock(mutex);
            if (!accepting || generation != state.generation) { return; }
            const auto old = state.Percent();
            state.done = done; state.total = total;
            if (old != state.Percent()) { ++state.revision; }
        }
        void Finish(std::uint64_t generation)
        {
            std::scoped_lock lock(mutex);
            if (!accepting || generation != state.generation) { return; }
            accepting = false; state.complete = true; ++state.revision;
        }
        void Stop(std::uint64_t generation)
        {
            std::scoped_lock lock(mutex);
            if (generation && generation == state.generation) { Hide(); }
        }
        void Clear(std::uint32_t owner = 0)
        {
            std::scoped_lock lock(mutex);
            if (!owner || owner == state.owner) { Hide(); }
        }
        [[nodiscard]] bool TryQueue()
        {
            std::scoped_lock lock(mutex);
            if (pending || delivered == state.revision) { return false; }
            pending = true;
            return true;
        }
        template<class Render> bool Deliver(Render&& render)
        {
            std::scoped_lock lock(mutex);
            pending = false;
            if (delivered == state.revision) { return false; }
            // Values are read here, when the UI task executes, not when queued.
            if (!render(state)) { return false; }
            delivered = state.revision;
            return true;
        }
        void QueueFailed() { std::scoped_lock lock(mutex); pending = false; }
    private:
        void Hide()
        {
            accepting = false;
            if (state.visible) { state.visible = false; ++state.revision; }
        }
        std::mutex mutex;
        DisplaySnapshot state;
        std::uint64_t delivered{};
        bool accepting{}, pending{};
    };
}
