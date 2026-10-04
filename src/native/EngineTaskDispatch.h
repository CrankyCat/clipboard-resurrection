// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <utility>

namespace Clipboard::EngineTaskDispatch
{
    // This marks only our F4SE task callbacks, not arbitrary engine/VM threads.
    inline thread_local bool inTask{};
    inline bool InTask() noexcept { return inTask; }
    struct Scope
    {
        bool previous{ inTask };
        Scope() noexcept { inTask = true; }
        ~Scope() { inTask = previous; }
    };

    inline bool Queue(std::function<void()> action) noexcept
    {
        try {
            const auto* tasks = F4SE::GetTaskInterface();
            if (!tasks || tasks->Version() < F4SE::TaskInterface::kVersion) { return false; }
            tasks->AddTask([action = std::move(action)]() mutable {
                Scope scope;
                action();
                // Release captures (including engine reference owners) in scope.
                action = {};
            });
            return true;
        } catch (...) { return false; }
    }

    template <class T>
    void Retire(T* value) noexcept
    {
        if (!value) { return; }
        if (InTask()) { delete value; return; }
        if (!Queue([value] { delete value; })) {
            // Shutdown/interface failure: deliberately retain the allocation until
            // process exit. Releasing engine references on a VM worker is unsafe.
            try { F4SE::log::error("Clipboard could not queue power-resource cleanup; allocation retained until process exit"); }
            catch (...) {}
        }
    }
}
