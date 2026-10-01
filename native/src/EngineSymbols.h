// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "RuntimeCompatibility.h"

#include <F4SE/F4SE.h>
#include <REL/Relocation.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace Clipboard::EngineAPI
{
    struct RequiredID
    {
        std::uint64_t key;  // Stable logical label; never used as a call address.
        REL::ID id;
        std::uint32_t v240RVA;  // Historical fixture provenance only.
        std::string_view name;
        bool requireOGLegacy{ false };
    };

    // Same runtime-aware declarations drive preflight and typed binding.
    // Explicit OG IDs come from pinned CommonLib or the reviewed F4SE/PE
    // audit. See Docs/Phase4/UNIFIED_RUNTIME_COMPATIBILITY.md.
    inline constexpr std::array kRequiredIDs{
        RequiredID{ 2193103, REL::ID(1376557, 2193103), 0x003123D0, "TESForm::GetFile" },
        RequiredID{ 2200893, REL::ID(817930, 2200893), 0x004FF8C0, "TESObjectREFR::SetScale" },
        RequiredID{ 2200260, REL::ID(1326073, 2200260), 0x004CAA60, "TESObjectCELL::GetbhkWorld" },
        RequiredID{ 2200179, REL::ID(868663, 2200179), 0x004C4E80, "TESObjectCELL::GetLocation" },
        RequiredID{ 2201150, REL::ID(1396707, 2201150), 0x00515970, "ObjectReference.Enable" },
        RequiredID{ 2201167, REL::ID(576133, 2201167), 0x005172E0, "TESObjectREFR::GetWorldSpace" },
        RequiredID{ 2201163, REL::ID(1135470, 2201163), 0x00517030, "TESObjectREFR::GetCurrentLocation" },
        RequiredID{ 2201196, REL::ID(1573130, 2201196), 0x005194C0, "TESObjectREFR::GetHandle" },
        RequiredID{ 2199660, REL::ID(958030, 2199660), 0x0049FCD0, "reference XLRL FormID lookup" },
        RequiredID{ 2204302, REL::ID(994198, 2204302), 0x005B4210, "ObjectReference.Disable" },
        RequiredID{ 2205201, REL::ID(652173, 2205201), 0x005E21A0, "EffectShader.Play" },
        RequiredID{ 2234097, REL::ID(631860, 2234097), 0x00DB5110, "EffectShader.Stop" },
        RequiredID{ 4796160, REL::ID(1569706, 4796160), 0x030E71C0, "ProcessLists singleton" },
        RequiredID{ 2194970, REL::ID(905705, 2194970), 0x00385720, "Workshop::FindNearestValidWorkshop" },
        RequiredID{ 2194996, REL::ID(1030944, 2194996), 0x00387090, "Workshop extra data AddItem" },
        RequiredID{ 2194998, REL::ID(523652, 2194998), 0x003876D0, "Workshop extra data AddConnection" },
        RequiredID{ 2195017, REL::ID(1523699, 2195017), 0x003897F0, "Workshop::ContextData constructor" },
        RequiredID{ 2195071, REL::ID(1150803, 2195071), 0x0038DB50, "SplineUtils::UpdateSpline" },
        RequiredID{ 2195073, REL::ID(59311, 2195073), 0x0038DFE0, "SplineUtils::ConnectSpline" },
        RequiredID{ 2195088, REL::ID(958345, 2195088), 0x0038ECB0, "PowerUtils::UpdateMovingWirelessItem" },
        RequiredID{ 2195102, REL::ID(608512, 2195102), 0x0038FB40, "TerminalUtils::EstablishTerminalLinks", true },
        RequiredID{ 2195125, REL::ID(636327, 2195125), 0x00393F30, "Workshop::ScrapReference" },
        RequiredID{ 2195571, REL::ID(1069718, 2195571), 0x003BE400, "Workshop::GetSnappedReferenceImpl" },
        RequiredID{ 2202683, REL::ID(897287, 2202683), 0x005648B0, "ObjectReference.GetLinkedRef" },
        RequiredID{ 2202684, REL::ID(192840, 2202684), 0x005648D0, "ObjectReference.SetLinkedRef" },
        RequiredID{ 2253499, REL::ID(984532, 2253499), 0x01159E80, "ObjectReference.PlaceAtMe" },
        RequiredID{ 2252785, REL::ID(1486694, 2252785), 0x01132830, "ScriptObject.CallFunctionNoWait" },
        RequiredID{ 2254251, REL::ID(1332434, 2254251), 0x01181020, "MoveRefrToPosition" },
        RequiredID{ 2253642, REL::ID(656686, 2253642), 0x01167410, "SetMotionTypeFunctor synchronous execution" },
        RequiredID{ 4795988, REL::ID(888641, 2688724, 4795988), 0x030E5280, "invalid reference handle" },
        RequiredID{ 4796135, REL::ID(711558, 4796135), 0x030E7100, "TESDataHandler singleton" },
        RequiredID{ 4796420, REL::ID(996227, 4796420), 0x030EB308, "GameVM singleton" },
        RequiredID{ 4796465, REL::ID(422985, 4796465), 0x030EBEC8, "all-forms map" },
        RequiredID{ 4796466, REL::ID(642758, 4796466), 0x030EBED0, "default object/editor-ID map" },
        RequiredID{ 4796476, REL::ID(691815, 4796476), 0x030EBF18, "all-forms map lock" },
        RequiredID{ 4796477, REL::ID(910917, 4796477), 0x030EBF20, "editor-ID map lock" },
        RequiredID{ 4797241, REL::ID(737927, 2689952, 4797241), 0x030F7698, "current workshop handle" },
        RequiredID{ 2698073, REL::ID(303410, 2698073), 0x032DD370, "PlayerCharacter singleton" },
        RequiredID{ 4839694, REL::ID(1008264, 4839694), 0x02F9D6E0, "NiExtraData RTTI" },
        RequiredID{ 4840294, REL::ID(1465949, 4840294), 0x02FA17C8, "ExtraLeveledCreature RTTI" },
        RequiredID{ 4859707, REL::ID(311239, 4859707), 0x030A9A98, "BSConnectPoint::Parents RTTI" },
    };

    [[nodiscard]] inline REL::IDResolveResult ResolveRequiredSymbol(const RequiredID& symbol)
    {
        auto& database = REL::IDDatabase::get();
        const auto runtime = REL::Module::get().version();
        if (symbol.requireOGLegacy &&
            REL::runtime_family(runtime) == REL::RuntimeFamily::kOG) {
            // The canonical terminal-link pattern falsely matches a different
            // OG function. Use its verified legacy ID only for the table's
            // documented runtime. Other OG versions require an exact known RVA:
            // numeric kNormal enters kKnownOnly on OG and skips pattern caches.
            // See the exact-executable evidence in UNIFIED_RUNTIME_COMPATIBILITY.
            const bool useLegacy = runtime == RuntimeCompatibility::kEmbeddedOGLegacyVersion;
            auto result = database.resolve(
                useLegacy ? symbol.id.og_id() : symbol.id.ae_id(), REL::IDResolveMode::kNormal);
            const auto requiredStatus = useLegacy ? REL::IDResolveStatus::kResolvedLegacy :
                REL::IDResolveStatus::kResolvedKnownRVA;
            if (result.status != requiredStatus) {
                result.rva.reset();
                result.finalRva.reset();
                result.status = REL::IDResolveStatus::kUnresolved;
            }
            return RuntimeCompatibility::CheckResolutionProvenance(result, runtime);
        }
        return RuntimeCompatibility::CheckResolutionProvenance(database.resolve(symbol.id), runtime);
    }

    [[nodiscard]] constexpr const RequiredID* FindRequiredSymbol(std::uint64_t key) noexcept
    {
        for (const auto& symbol : kRequiredIDs) {
            if (symbol.key == key) {
                return &symbol;
            }
        }
        return nullptr;
    }
}
