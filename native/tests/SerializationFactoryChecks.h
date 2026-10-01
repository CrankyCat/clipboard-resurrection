// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once
#include "F4SEObjectInterface.h"
#include "SerializationNames.h"

#include <cstring>
#include <string>
#include <unordered_map>

namespace Clipboard::SerializationFactoryChecks
{
	// Mirrors the reviewed public F4SE registry implementation: copy only the
	// factory vptr into stable map storage. No game code or heap is used.
	class Registry final : public F4SE::ObjectRegistry
	{
	public:
		bool RegisterFactory(F4SE::ObjectFactory* factory) override
		{
			std::uintptr_t vtable{};
			std::memcpy(&vtable, factory, sizeof(vtable));
			return factories.emplace(factory->ClassName(), vtable).second;
		}
		const F4SE::ObjectFactory* GetFactoryByName(const char* name) const override
		{
			const auto found = factories.find(name);
			return found == factories.end() ? nullptr :
				reinterpret_cast<const F4SE::ObjectFactory*>(std::addressof(found->second));
		}
		std::unordered_map<std::string, std::uintptr_t> factories;
	};

	template <const char* Canonical>
	class Object final : public F4SE::SerializableObject
	{
	public:
		explicit Object(F4SE::SerializationTag) { ++live; }
		~Object() override { --live; }
		const char* ClassName() const override { return Canonical; }
		std::uint32_t ClassVersion() const override { return 1; }
		bool Save(const F4SE::SerializationInterface*) override { output = payload; return true; }
		bool Load(const F4SE::SerializationInterface*, std::uint32_t version) override
		{
			if (version != ClassVersion()) { return false; }
			payload = input;
			return true;
		}
		static inline int live{};
		static inline std::uint32_t input{ 0xA531740B }, output{};
		std::uint32_t payload{};
	};

	template <const char* Canonical, const char* Legacy, class Check>
	void CheckPair(Registry& registry, Check&& check)
	{
		using Functor = Object<Canonical>;
		check(registry.RegisterClass<Functor>() && F4SE::RegisterReadAlias<Functor, Legacy>(registry),
			"canonical factory and legacy read alias register separately");
		const auto* canonical = registry.GetFactoryByName(Canonical);
		const auto* legacy = registry.GetFactoryByName(Legacy);
		check(canonical && legacy, "both saved-name generations locate a factory after its stack instance is gone");
		if (!canonical || !legacy) { return; }
		check(std::string_view(canonical->ClassName()) == Canonical && std::string_view(legacy->ClassName()) == Legacy,
			"vptr-only registered factory storage preserves each lookup identity");
		auto* loaded = legacy->Create();
		check(loaded && std::string_view(loaded->ClassName()) == Canonical,
			"legacy alias creates the canonical object, so a subsequent save writes the new name");
		if (loaded) {
			check(loaded->Load(nullptr, 1) && loaded->Save(nullptr) && Functor::output == Functor::input,
				"alias forwards the existing version and payload handling without rewriting payload");
			legacy->Free(loaded);
		}
		check(Functor::live == 0, "alias Free uses matching virtual deletion");
		auto* rejected = legacy->Create();
		check(rejected && !rejected->Load(nullptr, 2), "alias cannot bypass the object's unsupported payload-version rejection");
		if (rejected) { legacy->Free(rejected); }
		check(Functor::live == 0, "failed legacy load cleans up its object");
		check(!registry.RegisterClass<Functor>() && !F4SE::RegisterReadAlias<Functor, Legacy>(registry),
			"duplicate canonical or alias registration rejects instead of replacing an existing factory");
		auto* fresh = canonical->Create();
		check(fresh && std::string_view(fresh->ClassName()) == Canonical, "fresh operations use the canonical class name");
		if (fresh) { canonical->Free(fresh); }
		check(Functor::live == 0, "canonical factory and alias share safe object ownership");
	}

	template <class Check>
	void Run(Check&& check)
	{
		using namespace Clipboard::SerializationNames;
		Registry registry;
		CheckPair<kCreateSelectionBoxName, kCreateSelectionBoxLegacyName>(registry, check);
		CheckPair<kScrapSelectionName, kScrapSelectionLegacyName>(registry, check);
		CheckPair<kScrapObjectsName, kScrapObjectsLegacyName>(registry, check);
		CheckPair<kSendWorkshopEventName, kSendWorkshopEventLegacyName>(registry, check);
		CheckPair<kEnableObjectsName, kEnableObjectsLegacyName>(registry, check);
		CheckPair<kDisableObjectsName, kDisableObjectsLegacyName>(registry, check);
		CheckPair<kScaleSelectionName, kScaleSelectionLegacyName>(registry, check);
		CheckPair<kTransmitPowerName, kTransmitPowerLegacyName>(registry, check);
		CheckPair<kPastePatternObjectsName, kPastePatternObjectsLegacyName>(registry, check);
		CheckPair<kPastePatternWiresName, kPastePatternWiresLegacyName>(registry, check);
		CheckPair<kPastePatternWiresForRowsName, kPastePatternWiresForRowsLegacyName>(registry, check);
		CheckPair<kPrepareImportedObjectsName, kPrepareImportedObjectsLegacyName>(registry, check);
		CheckPair<kGetImportedPowerGeneratorsName, kGetImportedPowerGeneratorsLegacyName>(registry, check);
		CheckPair<kReconnectImportedPowerName, kReconnectImportedPowerLegacyName>(registry, check);
		CheckPair<kInitializeImportedWorkshopObjectsName, kInitializeImportedWorkshopObjectsLegacyName>(registry, check);
		check(registry.factories.size() == 30, "fifteen current factories and fifteen read aliases have distinct registry keys");
		check(registry.RegisterClass<Object<kScrapObjectsPacedName>>() &&
			registry.RegisterClass<Object<kGetImportedAnimationCandidatesName>>(), "new batch factories register under canonical names only");
		check(registry.factories.size() == 32 && !registry.GetFactoryByName("ClipboardResurrection.ScrapObjectsPacedFunctor"),
			"new operations do not manufacture legacy serialized aliases");
		check(registry.RegisterClass<Object<kConnectImportedPowerForRowsName>>() &&
			registry.RegisterClass<Object<kRefreshImportedPowerForRowsName>>(),
			"split power passes have separate canonical factories");
		check(registry.factories.size() == 34 &&
			!registry.GetFactoryByName("ClipboardResurrection.ConnectImportedPowerForRowsFunctor") &&
			!registry.GetFactoryByName("ClipboardResurrection.RefreshImportedPowerForRowsFunctor"),
			"diagnostic power passes cannot alias old saved operations");
	}
}
