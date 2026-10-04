// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#include "PCH.h"

#include "ImportAnimation.h"

#include <RE/NetImmerse/NiController.h>

namespace Clipboard::ImportAnimation
{
	namespace
	{
		[[nodiscard]] std::optional<bool> InspectGenericMotor(RE::TESObjectREFR* reference)
		{
			const auto* gameVM = RE::GameVM::GetSingleton();
			const auto vm = gameVM ? gameVM->GetVM() : nullptr;
			if (!vm) { return std::nullopt; }
			const auto& handles = vm->GetObjectHandlePolicy();
			const auto handle = handles.GetHandleForObject(RE::BSScript::GetVMTypeID<RE::TESObjectREFR>(), reference);
			if (handle == handles.EmptyHandle()) { return std::nullopt; }
			RE::BSTSmartPointer<RE::BSScript::Object> object;
			// Match OptionalFiltering's existing-bound-object lookup, including
			// script inheritance, without constructing or invoking a script.
			return vm->FindBoundObject(handle, "GenericMotorScript", true, object, false) && object;
		}

		[[nodiscard]] bool HasWorkshopPowerSequences(const RE::NiObjectNET* root)
		{
			auto* manager = RE::NiControllerManager::GetNiControllerManager(root);
			if (!manager) { return false; }
			// The sampled Workshop models expose this complete set in one
			// manager. Require every sequence to avoid matching arbitrary idle
			// controllers; sequence names are not graph-event return guarantees.
			for (const auto* name : { "PoweringUpOn", "UnpoweredOn", "TurningOn", "TurningOff" }) {
				if (!manager->GetSequenceByName(RE::BSFixedString{ name })) { return false; }
			}
			return true;
		}
	}

	std::optional<bool> HasGenericMotor(RE::TESObjectREFR* reference) noexcept
	{
		try {
			if (!reference || reference->As<RE::Actor>()) { return false; }
			if (reference->IsDeleted() || !reference->data.objectReference) { return std::nullopt; }
			return InspectGenericMotor(reference);
		} catch (...) {
			return std::nullopt;
		}
	}

	std::int32_t GetKind(RE::TESObjectREFR* reference) noexcept
	{
		try {
			if (!reference || reference->As<RE::Actor>()) { return 0; }
			if (reference->IsDeleted() || !reference->data.objectReference) { return kUnavailable; }
			const RE::NiPointer<RE::NiAVObject> root{ reference->Get3D() };
			if (!root) { return kUnavailable; }
			const auto motor = HasGenericMotor(reference);
			if (!motor) { return kUnavailable; }
			std::int32_t kind = *motor ? kGenericMotor : 0;
			if (HasWorkshopPowerSequences(root.get())) { kind |= kWorkshopPowerSequences; }
			return kind;
		} catch (...) {
			return kUnavailable;
		}
	}
}
