// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include "RuntimeCompatibility.h"
#include "LoggingPolicy.h"

#include <F4SE/F4SE.h>
#include <REL/Relocation.h>

#include <array>
#include <string_view>

namespace Clipboard::RuntimeSymbols
{
	struct Symbol
	{
		REL::ID id;
		std::string_view name;
		// Relative to external/CommonLibF4RD/CommonLibF4. The pinned source
		// supplies the function signature and Windows x64 calling convention.
		std::string_view provenance;
	};

	// Reachable CommonLib relocation surface reviewed against Clipboard.cpp,
	// EngineAPI.cpp, LatentBridge.cpp, LocalizationRuntime.cpp, LegacyCompat.h
	// LegacyPapyrusArray.h, OptionalFiltering.cpp and ImportAnimation.cpp.
	// Optional filtering and import animation reuse the form/VM/string/refcount
	// dependencies below. The inherited-script virtual lookup is audited in
	// Docs/OPTIONAL_FILTERING.md and Docs/OPTIONAL_FILTERING_ABI.md.
	// Keep the exact vendor family tuple: an OG fallback is not necessarily the
	// OG alias attached to the AE canonical database record (TESDataHandler).
	// Entries overlapping EngineAPI are intentional: its direct-call IDs and a
	// vendor wrapper can have different family fallbacks. No RVAs are called.
	inline constexpr std::array kCommonLibSymbols{
		// Strings, allocations and read locks reached by forms and Papyrus values.
		Symbol{ REL::ID(507142, 2268729), "BSStringPool::GetEntry<char>", "include/RE/Bethesda/BSStringPool.h" },
		Symbol{ REL::ID(1204430, 2268720), "BSStringPool::Entry::release", "include/RE/Bethesda/BSStringPool.h" },
		Symbol{ REL::ID(343176, 4471522), "MemoryManager::GetSingleton", "include/RE/Bethesda/MemoryManager.h" },
		Symbol{ REL::ID(652767, 2267872), "MemoryManager::Allocate", "include/RE/Bethesda/MemoryManager.h" },
		Symbol{ REL::ID(1582181, 2267874), "MemoryManager::Deallocate", "include/RE/Bethesda/MemoryManager.h" },
		Symbol{ REL::ID(1495205, 2267850), "MemoryManager::GetThreadScrapHeap", "include/RE/Bethesda/MemoryManager.h" },
		Symbol{ REL::ID(1085394, 2267983), "ScrapHeap::Allocate", "include/RE/Bethesda/MemoryManager.h" },
		Symbol{ REL::ID(923307, 2267984), "ScrapHeap::Deallocate", "include/RE/Bethesda/MemoryManager.h" },
		Symbol{ REL::ID(1573164, 2267897), "BSReadWriteLock::lock_read", "include/RE/Bethesda/BSLock.h" },
		Symbol{ REL::ID(1425657, 2192245), "BSSpinLock::lock (VM attachment snapshot)", "include/RE/Bethesda/BSLock.h" },
		Symbol{ REL::ID(226372, 2661402), "TESFullName::GetSparseFullNameMap (wrapper applies -0x8)", "include/RE/Bethesda/FormComponents.h" },
		// Same count engine used by stock GetItemCount/GetComponentCount, with
		// the stock component flag and zero-initialized out count. Called only
		// by ordinary non-tasklet Papyrus bindings; no inventory reconstruction.
		Symbol{ REL::ID(635042, 2200996), "TESObjectREFR::GetItemCount (component source counts)", "include/RE/Bethesda/TESObjectREFRs.h" },

		// Read-only sLanguage:General lookup after GameDataReady. Iterate past
		// the embedded list sentinel as official F4SE does; no virtual calls or
		// engine allocation/mutation methods are reached by this lookup.
		Symbol{ REL::ID(791183, 2704108), "INISettingCollection::GetSingleton", "include/RE/Bethesda/Settings.h" },
		Symbol{ REL::ID(767844, 2703234), "INIPrefSettingCollection::GetSingleton", "include/RE/Bethesda/Settings.h" },
		// Localized workshop progress notice, queued through F4SE AddTask.
		// void(const char*, const char*, bool, bool), Windows x64; message and
		// sound are copied by the engine during the call. See the lane experiment.
		Symbol{ REL::ID(1163005, 2222440), "SendHUDMessage::ShowHUDMessage", "include/RE/Bethesda/SendHUDMessage.h" },
		// Clipboard-owned progress TextField on the UI thread; no HUD asset edits.
		// UI/menu common prefix, GFx local ownership and virtual movie methods are
		// audited separately in Docs/IMPORT_PROGRESS_CALLBACK_REFRESH_2026_09_16.md.
		Symbol{ REL::ID(548587, 4796314), "UI::GetSingleton (progress HUD)", "include/RE/Bethesda/UI.h" },
		Symbol{ REL::ID(578487, 2707105), "UI::GetMenuMapRWLock (progress HUD)", "include/RE/Bethesda/UI.h" },
		// Dedicated ClipboardInputMenu. No TIM/SPECIALMenu replacement. The
		// IMenu common prefix and virtual GFx calls require separate live UI
		// acceptance; successful address resolution does not certify their ABI.
		Symbol{ REL::ID(1519575, 2284766), "UI::RegisterMenu (owned input)", "include/RE/Bethesda/UI.h" },
		Symbol{ REL::ID(106578, 4796889), "BSScaleformManager::GetSingleton", "include/RE/Bethesda/BSScaleformManager.h" },
		Symbol{ REL::ID(1526234, 2287422), "BSScaleformManager::LoadMovie", "include/RE/Bethesda/BSScaleformManager.h" },
		Symbol{ REL::ID(82123, 4796377), "UIMessageQueue::GetSingleton", "include/RE/Bethesda/UIMessageQueue.h" },
		Symbol{ REL::ID(1270833, 2284977), "BSUIMessageData::SendUIStringMessage", "include/RE/Bethesda/UIMessage.h" },
		Symbol{ REL::ID(325206, 4799307), "ControlMap::GetSingleton", "include/RE/Bethesda/ControlMap.h" },
		Symbol{ REL::ID(1270079, 4491359), "ControlMap::SetTextEntryMode", "include/RE/Bethesda/ControlMap.h" },
		Symbol{ REL::ID(1321764, 2222443), "SendHUDMessage::PushHUDMode", "include/RE/Bethesda/SendHUDMessage.h" },
		Symbol{ REL::ID(1495042, 2222444), "SendHUDMessage::PopHUDMode", "include/RE/Bethesda/SendHUDMessage.h" },
		// Clipboard uses explicit BSInputEventUser* adapters for both methods;
		// their engine entry points take the secondary base at IMenu + 0x10.
		Symbol{ REL::ID(1241790, 2287392), "IMenu::ShouldHandleEvent (secondary-base adapter)", "include/RE/Bethesda/IMenu.h" },
		Symbol{ REL::ID(1414130, 2287393), "IMenu::HandleEvent (secondary-base adapter)", "include/RE/Bethesda/IMenu.h" },
		Symbol{ REL::ID(150211, 2287395), "IMenu::ProcessScaleformEvent", "include/RE/Bethesda/IMenu.h" },
		Symbol{ REL::ID(1071829, 2287374), "IMenu::RefreshPlatform", "include/RE/Bethesda/IMenu.h" },
		Symbol{ REL::ID(937304, 2287379), "IMenu::PassesRenderConditionText", "include/RE/Bethesda/IMenu.h" },
		Symbol{ REL::ID(939898, 2707353), "Scaleform::Memory::GetGlobalHeap", "include/RE/Scaleform/Kernel/SF_Memory.h" },
		Symbol{ REL::ID(112478, 4847426), "RTTI::BSUIMessageData", "include/RE/RTTI_IDs.h" },
		Symbol{ REL::ID(635285, 4842122), "RTTI::IUIMessageData", "include/RE/RTTI_IDs.h" },
		Symbol{ REL::ID(244786, 2286228), "GFx::Value::ObjectInterface::ObjectAddRef", "include/RE/Scaleform/GFx/GFx_Player.h" },
		Symbol{ REL::ID(856221, 2286229), "GFx::Value::ObjectInterface::ObjectRelease", "include/RE/Scaleform/GFx/GFx_Player.h" },
		Symbol{ REL::ID(1517430, 4494126), "GFx::Value::ObjectInterface::GetMember", "include/RE/Scaleform/GFx/GFx_Player.h" },
		Symbol{ REL::ID(1360149, 2286589), "GFx::Value::ObjectInterface::SetMember", "include/RE/Scaleform/GFx/GFx_Player.h" },
		Symbol{ REL::ID(655847, 2286101), "GFx::Value::ObjectInterface::Invoke", "include/RE/Scaleform/GFx/GFx_Player.h" },
		// Winning PEX resource identity for the conditional-no-op experiment.
		// Pinned Windows x64 signatures, adapted with opaque engine ownership;
		// see ResourceStreamABI.h and the conditional-no-op ABI audit.
		Symbol{ REL::ID(1198116, 2269830), "BSResourceNiBinaryStream::ctor", "src/RE/Bethesda/BSResourceNiBinaryStream.cpp" },
		Symbol{ REL::ID(1516202, 2269832), "BSResourceNiBinaryStream::dtor", "src/RE/Bethesda/BSResourceNiBinaryStream.cpp" },
		Symbol{ REL::ID(265501, 2269836), "BSResourceNiBinaryStream::GetBufferInfo", "src/RE/Bethesda/BSResourceNiBinaryStream.cpp" },
		Symbol{ REL::ID(424286, 2269839), "BSResourceNiBinaryStream::DoRead", "src/RE/Bethesda/BSResourceNiBinaryStream.cpp" },

		// Registration, dispatch, serialization, marshalling and final releases.
		// Variable/Array/Struct destruction is implemented locally by CommonLib;
		// its transitive Object refcounts, ObjectTypeInfo destruction and heap
		// operations below are the engine relocations those destructors reach.
		Symbol{ REL::ID(996227, 4796420), "GameVM::GetSingleton", "include/RE/Bethesda/GameScript.h" },
		Symbol{ REL::ID(571037, 2314780), "BSScript::NF_util::NativeFunctionBase::Call", "include/RE/Bethesda/BSScript/NF_util/NativeFunctionBase.h" },
		Symbol{ REL::ID(1429302, 2314680), "BSScript::Stack::GetPageForFrame", "include/RE/Bethesda/BSScript/Stack.h" },
		Symbol{ REL::ID(897539, 2314681), "BSScript::Stack::GetStackFrameVariable", "include/RE/Bethesda/BSScript/Stack.h" },
		Symbol{ REL::ID(709728, 2314370), "BSScript::ObjectBindPolicy::BindObject", "include/RE/Bethesda/BSScript/ObjectBindPolicy.h" },
		Symbol{ REL::ID(1452752, 2314431), "BSScript::Object::GetHandle", "include/RE/Bethesda/BSScript/Object.h" },
		Symbol{ REL::ID(461710, 2314436), "BSScript::Object::IncRef", "include/RE/Bethesda/BSScript/Object.h" },
		Symbol{ REL::ID(541793, 2314437), "BSScript::Object::DecRef", "include/RE/Bethesda/BSScript/Object.h" },
		Symbol{ REL::ID(1047917, 2314513), "BSScript::ObjectTypeInfo::Dtor", "src/RE/Bethesda/BSScript/ObjectTypeInfo.cpp" },

		// Form discovery, display names, reference transforms and workshop calls.
		Symbol{ REL::ID(711558, 4796135), "TESDataHandler::GetSingleton", "include/RE/Bethesda/TESDataHandler.h" },
		Symbol{ REL::ID(1376557, 2193103), "TESForm::GetFile", "include/RE/Bethesda/TESForms.h" },
		Symbol{ REL::ID(422985, 4796465), "TESForm::GetAllForms map", "include/RE/Bethesda/TESForms.h" },
		Symbol{ REL::ID(691815, 4796476), "TESForm::GetAllForms lock", "include/RE/Bethesda/TESForms.h" },
		Symbol{ REL::ID(642758, 4796466), "TESForm::GetAllFormsByEditorID map", "include/RE/Bethesda/TESForms.h" },
		Symbol{ REL::ID(910917, 4796477), "TESForm::GetAllFormsByEditorID lock", "include/RE/Bethesda/TESForms.h" },
		Symbol{ REL::ID(1326073, 2200260), "TESObjectCELL::GetbhkWorld", "include/RE/Bethesda/TESForms.h" },
		Symbol{ REL::ID(576133, 2201167), "TESObjectREFR::GetWorldSpace", "include/RE/Bethesda/TESObjectREFRs.h" },
		Symbol{ REL::ID(1573130, 2201196), "TESObjectREFR::GetHandle", "include/RE/Bethesda/TESObjectREFRs.h" },
		// Generation-aware wire endpoint resolution at each engine-task slice.
		Symbol{ REL::ID(967277, 2188681), "BSPointerHandleManagerInterface::GetSmartPointer", "include/RE/Bethesda/BSPointerHandle.h" },
		Symbol{ REL::ID(817930, 2200893), "TESObjectREFR::SetScale", "include/RE/Bethesda/TESObjectREFRs.h" },
		Symbol{ REL::ID(303410, 2698073), "PlayerCharacter::GetSingleton", "include/RE/Bethesda/PlayerCharacter.h" },
		Symbol{ REL::ID(905705, 2194970), "Workshop::FindNearestValidWorkshop", "include/RE/Bethesda/Workshop.h" },
		Symbol{ REL::ID(990965, 2194956), "Workshop::IsLocationWithinBuildableArea", "include/RE/Bethesda/Workshop.h" },
		Symbol{ REL::ID(1569706, 4796160), "ProcessLists::GetSingleton", "include/RE/Bethesda/ProcessLists.h" },

		// Read-only loaded-model capability checks; neither call starts or
		// restarts a sequence. Preserve the exact pinned vendor family tuples.
		Symbol{ REL::ID(1013515, 2271798), "NiControllerManager::GetNiControllerManager", "include/RE/NetImmerse/NiController.h" },
		Symbol{ REL::ID(846648, 2192808), "NiControllerManager::GetSequenceByName", "include/RE/NetImmerse/NiController.h" },

		// Clipboard's actual fallout_cast: NiExtraData -> BSConnectPoint::Parents.
		// TESForm::As and ExtraDataList::GetByType use static/type-tag casts, so
		// their unused RTTI/VTABLE declarations do not belong in this inventory.
		Symbol{ REL::ID(84112, 4818455), "RTDynamicCast", "include/RE/RTTI.h" },
		Symbol{ REL::ID(1008264, 4839694), "RTTI::NiExtraData", "include/RE/RTTI_IDs.h" },
		Symbol{ REL::ID(311239, 4859707), "RTTI::BSConnectPoint__Parents", "include/RE/RTTI_IDs.h" },
	};

	[[nodiscard]] inline REL::IDResolveResult ResolveSymbol(const Symbol& symbol)
	{
		return RuntimeCompatibility::CheckResolutionProvenance(
			REL::IDDatabase::get().resolve(symbol.id), REL::Module::get().version());
	}

	// Run after F4SE::Init, before any Papyrus wrapper construction/registration.
	// resolve() returns failures without invoking REL::ID::offset/report_and_fail.
	// Successful resolutions populate the same database cache later wrappers use.
	// This guards the reviewed plugin-side relocation closure, not vtable slots,
	// layouts, F4SE interface callbacks, engine-internal calls or future code paths.
	[[nodiscard]] inline bool ResolveRequiredCommonLibSymbols()
	{
		bool valid = true;
		for (const auto& symbol : kCommonLibSymbols) {
			const auto result = ResolveSymbol(symbol);
			if (!result || !result.rva) {
				F4SE::log::critical(
					"Required CommonLib Runtime Database symbol {} (OG {}, NG {}, AE {}; {}) failed: {}",
					symbol.name, symbol.id.og_id(), symbol.id.ng_id(), symbol.id.ae_id(),
					symbol.provenance, REL::id_resolve_status_text(result.status));
				valid = false;
			}
		}
		if (valid) {
			F4SE::log::info("Resolved all {} reviewed CommonLib Runtime Database symbols", kCommonLibSymbols.size());
		}
		return valid;
	}
}
