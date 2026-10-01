// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once
#include "ImportProgressChannel.h"
#include "LocalizationRuntime.h"
#include "LoggingPolicy.h"
#include <RE/Bethesda/UI.h>
#include <cmath>

namespace Clipboard::ImportProgress
{
    inline DisplayChannel g_displayChannel;

    // UI-thread-only. All GFx values and the menu's strong reference are local
    // to this call. No cached movie pointers, custom SWF or HUD queue edits.
    inline bool RenderPercentage(const DisplaySnapshot& state)
    {
        using Value = RE::Scaleform::GFx::Value;
        static_assert(offsetof(RE::IMenu, uiMovie) == 0x40);
        static_assert(offsetof(RE::Scaleform::GFx::Movie, asMovieRoot) == 0x18);
        const auto* ui = RE::UI::GetSingleton();
        const auto menu = ui ? ui->GetMenu(RE::BSFixedString("HUDMenu")) : nullptr;
        if (!menu || !menu->uiMovie) { return !state.visible; }
        auto* movie = menu->uiMovie.get();
        Value root, parent, messages;
        // Standard AS3 root and the legacy alias; require the known HUD parent.
        for (const auto path : { "root", "_root", "root1" }) {
            if (movie->GetVariable(&root, path) && root.IsObject() &&
                root.GetMember("HUDNotificationsGroup_mc", &parent) && parent.IsObject()) { break; }
        }
        if (!parent.IsObject()) { return !state.visible; }
        constexpr auto name = "ClipboardImportProgressText";
        const Value nameArg(name);
        Value field;
        if (!parent.Invoke("getChildByName", &field, &nameArg, 1)) { return false; }
        if (!state.visible) {
            return !field.IsObject() || parent.Invoke("removeChild", nullptr, &field, 1);
        }
        if (!field.IsObject()) {
            movie->CreateObject(&field, "flash.text.TextField");
            if (!field.IsObject()) { return false; }
            Value format;
            const Value formatArgs[]{ Value("$MAIN_Font"), Value(22.0), Value(std::uint32_t(0xFFFFFF)) };
            movie->CreateObject(&format, "flash.text.TextFormat", formatArgs, 3);
            if (!format.IsObject() || !field.SetMember("defaultTextFormat", format) ||
                !field.SetMember("name", nameArg) || !field.SetMember("autoSize", Value("left")) ||
                !field.SetMember("selectable", Value(false)) || !field.SetMember("mouseEnabled", Value(false)) ||
                !field.SetMember("multiline", Value(false)) || !field.SetMember("wordWrap", Value(false)) ||
                !parent.Invoke("addChild", nullptr, &field, 1)) { return false; }
        }
        // Move one rendered line below the position used by the tested HUD.
        // Measure after assigning text so autoSize has the actual font height.
        // Never feed progress into the HUD's notification buffers.
        double x = 20.0, y = 40.0;
        Value position;
        if (parent.GetMember("Messages_mc", &messages) && messages.IsObject()) {
            if (messages.GetMember("x", &position) && position.IsNumber()) { x = position.GetNumber(); }
            if (messages.GetMember("y", &position) && position.IsNumber()) { y = position.GetNumber(); }
        }
        const auto key = state.phase == Phase::Workshop ? "$Clipboard_WorkshopInitializationProgress" : "$Clipboard_PoweringUpProgress";
        const auto text = Localization::GetRuntimeText(key, { std::to_string(state.Percent()) });
        if (!field.SetMember("text", Value(text.c_str()))) { return false; }
        double lineHeight = 32.0;
        Value measuredHeight;
        if (field.GetMember("height", &measuredHeight) && measuredHeight.IsNumber()) {
            const auto height = measuredHeight.GetNumber();
            if (std::isfinite(height) && height > 0.0) { lineHeight = height; }
        }
        if (!field.SetMember("x", Value(x)) || !field.SetMember("y", Value(std::max(0.0, y - 32.0) + lineHeight)) ||
            !field.SetMember("visible", Value(true))) { return false; }
        Value readback;
        return field.GetMember("text", &readback) && readback.IsString() && text == readback.GetString();
    }

    inline void QueuePercentageRefresh() noexcept
    {
        try {
            const auto* tasks = F4SE::GetTaskInterface();
            if (!tasks || tasks->Version() < F4SE::TaskInterface::kVersion || !g_displayChannel.TryQueue()) { return; }
            tasks->AddUITask([]() noexcept {
                try {
                    g_displayChannel.Deliver([](const DisplaySnapshot& state) {
                        const bool rendered = RenderPercentage(state);
                        // At most one unavailable warning per generation. Every
                        // successful integer change has a bounded readback log.
                        static std::uint64_t warnedGeneration{};
                        if (rendered) {
                            CLIPBOARD_DEBUG_LOG(F4SE::log::info("Clipboard progress HUD: owner {:08X}, generation {}, revision {}, phase {}, completed units {}, total units {}, percent {}, visible {}; direct text readback, not queued notification",
                                state.owner, state.generation, state.revision, state.phase == Phase::Workshop ? "workshop" : "power", state.done, state.total, state.Percent(), state.visible));
                        } else if (warnedGeneration != state.generation) {
                            warnedGeneration = state.generation;
                            F4SE::log::warn("Clipboard progress HUD unavailable: owner {:08X}, generation {}; import unchanged, latest value retained for retry", state.owner, state.generation);
                        }
                        return rendered;
                    });
                } catch (...) { g_displayChannel.QueueFailed(); }
            });
        } catch (...) { g_displayChannel.QueueFailed(); }
    }

    inline void ClearPercentage(std::uint32_t owner = 0) noexcept
    {
        g_displayChannel.Clear(owner);
        QueuePercentageRefresh();
    }
}
