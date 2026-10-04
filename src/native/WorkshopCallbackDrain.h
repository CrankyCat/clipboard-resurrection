// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace Clipboard::WorkshopCallbacks
{
    // Runtime admission policy only. Restored unfinished jobs never dispatch.
    // Close on accepted pending calls, not transient reservations or omissions.
    class CallbackDrain
    {
    public:
        explicit CallbackDrain(std::uint32_t high = 0, bool eligible = false, std::int32_t resumeAt = -1) noexcept :
            high_(high), enabled_(eligible && high > 0 && resumeAt >= 0 &&
                static_cast<std::uint32_t>(resumeAt) < high), low_(enabled_ ? static_cast<std::uint32_t>(resumeAt) : 0) {}

        void Accepted(std::uint32_t pending, std::uint64_t nowMs) noexcept
        {
            if (enabled_ && !closed_ && pending >= high_) {
                closed_ = true;
                closedSince_ = nowMs;
                ++closures_;
            }
        }
        void Resolved(std::uint32_t pending, std::uint64_t nowMs) noexcept
        {
            if (closed_ && pending <= low_) {
                lastHoldMs_ = nowMs >= closedSince_ ? nowMs - closedSince_ : 0;
                heldMs_ += lastHoldMs_;
                closed_ = false;
                ++reopenings_;
            }
        }
        [[nodiscard]] bool Allows() const noexcept { return !closed_; }
        [[nodiscard]] bool Enabled() const noexcept { return enabled_; }
        [[nodiscard]] bool Closed() const noexcept { return closed_; }
        [[nodiscard]] std::uint32_t High() const noexcept { return high_; }
        [[nodiscard]] std::uint32_t ResumeAt() const noexcept { return low_; }
        [[nodiscard]] std::uint64_t Closures() const noexcept { return closures_; }
        [[nodiscard]] std::uint64_t Reopenings() const noexcept { return reopenings_; }
        [[nodiscard]] std::uint64_t LastHoldMs() const noexcept { return lastHoldMs_; }
        [[nodiscard]] std::uint64_t HeldMs(std::uint64_t nowMs) const noexcept
        {
            return heldMs_ + (closed_ && nowMs >= closedSince_ ? nowMs - closedSince_ : 0);
        }
    private:
        std::uint32_t high_{};
        bool enabled_{}, closed_{};
        std::uint32_t low_{};
        std::uint64_t closures_{}, reopenings_{}, closedSince_{}, heldMs_{}, lastHoldMs_{};
    };
}
