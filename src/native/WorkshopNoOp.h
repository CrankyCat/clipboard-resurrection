// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once
#include "WorkshopNoOpPolicy.h"

namespace Clipboard::WorkshopNoOp
{
    struct Resources
    {
        unsigned workshopRevision{}, parentRevision{};
        std::string workshopSHA, parentSHA, vaultWorkshopSHA;
        [[nodiscard]] bool Compatible() const noexcept { return workshopRevision && parentRevision; }
    };
    [[nodiscard]] Resources InspectResources() noexcept;
    [[nodiscard]] bool CompatibleReceiver(RE::BSScript::IVirtualMachine& vm, RE::TESObjectREFR* workshop, const Resources& resources) noexcept;
    [[nodiscard]] Kind Classify(RE::BSScript::IVirtualMachine& vm, RE::TESObjectREFR* reference) noexcept;
}
