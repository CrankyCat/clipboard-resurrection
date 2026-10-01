// SPDX-License-Identifier: GPL-3.0-or-later
// Included in Clipboard.cpp after the maintained form-ID and VM helpers.

namespace
{
	constexpr std::size_t kMaximumLegacyCleanupReferences = 4096;
	// Verified from package/v240/Clipboard.esp ACTI EDID records, 2026-09-11.
	// These are plugin-local IDs, never VM handles or saved reference IDs.
	constexpr std::array<std::uint32_t, 12> kLegacyCleanupBases{
		0x2E05, 0x343C8, 0x31DBC, 0x5411, 0x32556,
		0x380A7, 0x380A6, 0x38FEA, 0x380A8, 0x635A, 0x6359, 0x380A9
	};
	std::mutex legacyCleanupSnapshotLock;
	std::int32_t legacyCleanupSnapshotToken{};
	std::vector<RE::NiPointer<RE::TESObjectREFR>> legacyCleanupSnapshot;

	bool IsKnownLegacyCleanupBase(RE::TESForm* form)
	{
		if (!form || !form->Is(RE::ENUM_FORM_ID::kACTI)) { return false; }
		const auto fullID = GetFullFormId(form->formID & 0xFFFFFF, "Clipboard.esp");
		return fullID == form->formID && std::find(kLegacyCleanupBases.begin(), kLegacyCleanupBases.end(),
			form->formID & 0xFFFFFF) != kLegacyCleanupBases.end();
	}
}

void ResetLegacyCleanupSnapshot(std::int32_t token)
{
	std::vector<RE::NiPointer<RE::TESObjectREFR>> released;
	{
		std::scoped_lock lock(legacyCleanupSnapshotLock);
		if (token > 0 && legacyCleanupSnapshotToken != token) { return; }
		released.swap(legacyCleanupSnapshot);
		legacyCleanupSnapshotToken = 0;
	}
	// Final engine-reference releases never occur while our snapshot lock is held.
}

std::int32_t BeginLegacyCleanup(StaticFunctionTag*)
{
	const auto token = Clipboard::Latent::BeginLegacyCleanup();
	if (token > 0) {
		std::vector<RE::NiPointer<RE::TESObjectREFR>> released;
		{
			std::scoped_lock lock(legacyCleanupSnapshotLock);
			if (!Clipboard::Latent::IsLegacyCleanupActive(token)) { return 0; }
			released.swap(legacyCleanupSnapshot);
			legacyCleanupSnapshotToken = token;
		}
	}
	return token;
}

bool IsLegacyCleanupActive(StaticFunctionTag*, std::int32_t token)
{
	return Clipboard::Latent::IsLegacyCleanupActive(token);
}

void EndLegacyCleanup(StaticFunctionTag*, std::int32_t token)
{
	if (token <= 0 || !Clipboard::Latent::IsLegacyCleanupActive(token)) { return; }
	ResetLegacyCleanupSnapshot(token);
	Clipboard::Latent::EndLegacyCleanup(token);
}

VMArray<RE::TESObjectREFR*> GetLegacyCleanupCandidatesImpl(std::int32_t token)
{
	if (token <= 0 || !Clipboard::Latent::IsLegacyCleanupActive(token)) { return { nullptr }; }
	const auto gameVM = RE::GameVM::GetSingleton();
	const auto vmOwner = gameVM ? gameVM->GetVM() : nullptr;
	if (!vmOwner) { return { nullptr }; }
	auto& vm = static_cast<RE::BSScript::Internal::VirtualMachine&>(*vmOwner);
	// Same attachment layout and virtual handle policy as the reviewed manual
	// selection snapshot; no all-world-reference traversal or handle truncation.
	static_assert(offsetof(RE::BSScript::Internal::VirtualMachine, attachedScriptsLock) == 0xBDF8);
	static_assert(offsetof(RE::BSScript::Internal::VirtualMachine, attachedScripts) == 0xBE00);
	const std::array<RE::BSFixedString, 4> names{
		"ClipboardCopyPylonScript", "ClipboardManager", "ClipboardButtonStand", "ClipboardSelectionMethod"
	};
	std::vector<std::uint64_t> handles;
	{
		const RE::BSAutoLock lock{ vm.attachedScriptsLock };
		handles = Clipboard::OptionalFiltering::CollectAttachedScriptCandidates(vm.attachedScripts, names,
			[](const auto& object) -> const RE::BSScript::ObjectTypeInfo* { return object ? object->type.get() : nullptr; },
			[](const auto* type) -> const RE::BSFixedString& { return type->name; },
			[](const auto* type) -> const RE::BSScript::ObjectTypeInfo* { return type->parentTypeInfo.get(); },
			kMaximumLegacyCleanupReferences);
	}
	const auto& policy = vm.GetObjectHandlePolicy();
	std::vector<RE::NiPointer<RE::TESObjectREFR>> references;
	references.reserve(handles.size());
	for (const auto handle : handles) {
		auto* reference = static_cast<RE::TESObjectREFR*>(policy.GetObjectForHandle(
			RE::BSScript::GetVMTypeID<RE::TESObjectREFR>(), handle));
		if (!reference) {
			logger::warn("Legacy cleanup aborted: attached Clipboard VM handle {:016X} could not resolve; no deletion attempted", handle);
			return { nullptr };
		}
		references.emplace_back(reference);
	}
	{
		const auto [forms, lock] = RE::TESForm::GetAllForms();
		const RE::BSAutoReadLock readLock{ lock };
		if (!forms) { return { nullptr }; }
		for (const auto& reference : references) {
			const auto found = forms->find(reference->formID);
			if (found == forms->end() || found->second != reference.get() || !reference->Is(RE::ENUM_FORM_ID::kREFR)) {
				return { nullptr };
			}
		}
	}
	std::sort(references.begin(), references.end(), [](const auto& a, const auto& b) { return a->formID < b->formID; });
	references.erase(std::unique(references.begin(), references.end()), references.end());
	VMArray<RE::TESObjectREFR*> result;
	for (const auto& reference : references) { result.push_back(reference.get()); }
	if (!Clipboard::Latent::IsLegacyCleanupActive(token)) { return { nullptr }; }
	{
		std::scoped_lock lock(legacyCleanupSnapshotLock);
		if (legacyCleanupSnapshotToken != token || !legacyCleanupSnapshot.empty()) { return { nullptr }; }
		legacyCleanupSnapshot.swap(references);
	}
	if (enableLogging) { CLIPBOARD_DEBUG_LOG(logger::info("Legacy cleanup discovered {} attached Clipboard references; snapshot held until this session ends", result.size())); }
	return result;
}

VMArray<RE::TESObjectREFR*> GetLegacyCleanupCandidates(StaticFunctionTag*, std::int32_t token)
{
	try { return GetLegacyCleanupCandidatesImpl(token); }
	catch (...) {
		// An incomplete inventory must never masquerade as successful no-work.
		logger::warn("Legacy cleanup snapshot failed or exceeded {} candidates; no deletion attempted", kMaximumLegacyCleanupReferences);
		return { nullptr };
	}
}

bool IsLegacyCleanupReference(StaticFunctionTag*, RE::TESObjectREFR* reference, RE::TESForm* expectedBase, std::int32_t token)
{
	if (token <= 0 || !Clipboard::Latent::IsLegacyCleanupActive(token) || !reference ||
		(reference->formID & 0xFF000000) != 0xFF000000 || reference->data.objectReference != expectedBase ||
		!IsKnownLegacyCleanupBase(expectedBase)) { return false; }
	{
		const auto [forms, lock] = RE::TESForm::GetAllForms();
		const RE::BSAutoReadLock readLock{ lock };
		if (!forms) { return false; }
		const auto current = forms->find(reference->formID);
		if (current == forms->end() || current->second != reference) { return false; }
	}
	std::scoped_lock lock(legacyCleanupSnapshotLock);
	return legacyCleanupSnapshotToken == token &&
		std::any_of(legacyCleanupSnapshot.begin(), legacyCleanupSnapshot.end(),
			[reference](const auto& captured) { return captured.get() == reference; });
}
