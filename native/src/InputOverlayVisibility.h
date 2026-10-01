// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <utility>

namespace Clipboard::Input
{
    // UI-lane ownership of an outer display object's visibility. The overlay's
    // own child visibility remains independent and may change while suppressed.
    // Handle retains the movie, so a removed/replaced menu cannot leave a raw
    // GFx pointer behind. Reader returns optional<bool>; writer reports success.
    template <class Handle>
    class OverlayVisibility
    {
    public:
        template <class Read, class Write>
        void Update(Handle candidate, Read&& read, Write&& write)
        {
            if (candidate == target_) return;
            Release(write);
            if (!candidate) return;
            const auto visible = read(candidate);
            if (!visible.has_value()) return;
            if (*visible && !write(candidate, false)) return;
            target_ = std::move(candidate);
            restoreVisible_ = *visible;
        }

        template <class Write>
        bool Release(Write&& write)
        {
            // Clear ownership before calling out; duplicate cleanup cannot
            // restore a later menu instance or perform a second write.
            auto previous = std::move(target_);
            target_ = {};
            const bool restore = std::exchange(restoreVisible_, false);
            return !previous || !restore || write(previous, true);
        }

    private:
        Handle target_;
        bool restoreVisible_{};
    };
}
