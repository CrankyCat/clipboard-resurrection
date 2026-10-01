// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <F4SE/F4SE.h>
#include "LoggingPolicy.h"
#include <REL/Relocation.h>

#include <cstdint>
#include <string_view>

namespace Clipboard::RuntimeCompatibility
{
    // The unversioned embedded table's runtime is documented by the pinned
    // CommonLibF4RD docs/FEATURES.md, not encoded in its count/ID/RVA prefix.
    inline constexpr REL::Version kEmbeddedOGLegacyVersion{ 1, 10, 163, 0 };

    [[nodiscard]] inline REL::IDResolveResult CheckResolutionProvenance(
        REL::IDResolveResult result, const REL::Version& runtime)
    {
        if (result.status == REL::IDResolveStatus::kResolvedLegacy &&
            runtime != kEmbeddedOGLegacyVersion) {
            F4SE::log::critical(
                "Runtime Database ID {} used the embedded OG {} table on runtime {}; a runtime-specific resolution is required",
                result.id, kEmbeddedOGLegacyVersion.string(), runtime.string());
            result.rva.reset();
            result.finalRva.reset();
            result.status = REL::IDResolveStatus::kUnresolved;
        }
        return result;
    }

    [[nodiscard]] constexpr bool SupportsDeclaredLayout(const REL::Version& runtime) noexcept
    {
        switch (REL::runtime_family(runtime)) {
        case REL::RuntimeFamily::kOG:
        case REL::RuntimeFamily::kNG:
        case REL::RuntimeFamily::kAE:
            return true;
        default:
            return false;
        }
    }

    inline constexpr std::uint32_t kStructureFlags =
        F4SE::PluginVersionData::kStructureIndependence_1_10_980Layout |
        F4SE::PluginVersionData::kStructureIndependence_1_11_137Layout;

    [[nodiscard]] constexpr std::string_view FamilyName(const REL::Version& runtime) noexcept
    {
        switch (REL::runtime_family(runtime)) {
        case REL::RuntimeFamily::kOG: return "OG";
        case REL::RuntimeFamily::kNG: return "NG";
        case REL::RuntimeFamily::kAE: return "AE";
        default: return "unknown";
        }
    }
}
