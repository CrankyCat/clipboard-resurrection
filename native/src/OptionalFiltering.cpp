// SPDX-License-Identifier: GPL-3.0-or-later
#include "PCH.h"
#include "LoggingPolicy.h"

#include "OptionalFiltering.h"

#include "AttachedScriptCandidates.h"
#include "CachedReadLock.h"
#include "EngineAPI.h"
#include "OptionalChildOwnership.h"
#include "OptionalFilteringPolicy.h"
#include "OptionalLinkedRefKeywords.h"

#include <unordered_set>

namespace Clipboard::OptionalFiltering
{
	namespace
	{
		struct Configuration
		{
			std::vector<RE::BGSKeyword*> referenceKeywords;
			std::vector<std::string> scolFamilyRoots;
			std::vector<RE::BGSKeyword*> childLinkKeywords;
			std::unordered_set<std::uintptr_t> childLinkKeywordPointers;
			std::vector<std::string> parentScripts;
		};

		struct ScanState
		{
			std::shared_ptr<const Configuration> configuration;
			bool indexed{ false };
			ChildOwnershipIndex<RE::NiPointer<RE::TESObjectREFR>, RE::BGSKeyword*> childLinks;
		};

		std::mutex configurationMutex;
		std::shared_ptr<const Configuration> configuration{ std::make_shared<Configuration>() };
		thread_local std::unique_ptr<ScanState> scan;
		thread_local std::size_t scanDepth{ 0 };

		[[nodiscard]] std::string Trim(std::string_view value)
		{
			const auto first = value.find_first_not_of(" \t\r\n");
			return first == std::string_view::npos ? std::string{} :
				std::string{ value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1) };
		}

		void AppendUnique(std::vector<std::string>& values, std::string value)
		{
			if (!value.empty() && std::none_of(values.begin(), values.end(), [&](const auto& entry) {
					return _stricmp(entry.c_str(), value.c_str()) == 0;
				})) {
				values.push_back(std::move(value));
			}
		}

		[[nodiscard]] std::vector<RE::BGSKeyword*> ResolveKeywords(
			const std::vector<std::string>& entries, std::string_view category)
		{
			std::vector<RE::BGSKeyword*> result;
			auto* data = RE::TESDataHandler::GetSingleton();
			for (const auto& entry : entries) {
				if (Trim(entry).empty()) {
					continue;
				}
				const auto spec = Policy::ParseFormSpec(entry);
				if (!spec) {
					logger::warn("Optional filter {} skipped malformed plugin#decimalForm entry: {}", category, entry);
					continue;
				}
				const auto* file = data ? data->LookupModByName(spec->plugin) : nullptr;
				if (!file || !file->IsActive()) {
					CLIPBOARD_DEBUG_LOG(logger::info("Optional filter {} skipped unavailable plugin: {}", category, spec->plugin));
					continue;
				}
				if (file->IsLight() && spec->localFormID > 0xFFF) {
					logger::warn("Optional filter {} skipped out-of-range light-plugin form: {}", category, entry);
					continue;
				}
				auto* keyword = data->LookupForm<RE::BGSKeyword>(spec->localFormID, spec->plugin);
				if (!keyword) {
					logger::warn("Optional filter {} skipped unresolved/non-KYWD form: {}", category, entry);
					continue;
				}
				if (std::find(result.begin(), result.end(), keyword) == result.end()) {
					result.push_back(keyword);
				}
			}
			return result;
		}

		[[nodiscard]] bool HasParentScript(
			RE::BSScript::IVirtualMachine& vm,
			const RE::TESObjectREFR* reference,
			const std::vector<std::string>& scripts)
		{
			const auto& handles = vm.GetObjectHandlePolicy();
			const auto handle = handles.GetHandleForObject(RE::BSScript::GetVMTypeID<RE::TESObjectREFR>(), reference);
			if (handle == handles.EmptyHandle()) {
				return false;
			}
			for (const auto& script : scripts) {
				RE::BSTSmartPointer<RE::BSScript::Object> object;
				// Find an existing attached object, including a derived script.
				// Never load a type, CreateObject, bind it or invoke its functions.
				if (vm.FindBoundObject(handle, script.c_str(), true, object, false) && object) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool MayHaveConfiguredChildSlot(
			const RE::TESObjectREFR* parent, const Configuration& settings)
		{
			if (!parent->extraList) {
				return false;
			}
			// Keep the prior LinkedRef admission guard. Hold only this parent's
			// extra-data lock while scanning keyword evidence, never the form-map
			// lock and never across VM or GetLinkedRef engine calls.
			const auto& list = *parent->extraList;
			const CachedReadLock readLock{ list.extraRWLock };
			if (!list.extraData.HasType(RE::EXTRA_DATA_TYPE::kLinkedRef)) {
				return false;
			}
			// Alias links already require conservative admission. Their direct
			// LinkedRef payload would be ignored, so avoid an unused list walk.
			if (list.extraData.HasType(RE::EXTRA_DATA_TYPE::kAliasInstanceArray)) {
				return true;
			}
			const auto* extra = list.extraData.GetByType(RE::EXTRA_DATA_TYPE::kLinkedRef);
			static_assert(sizeof(RE::BSExtraData) == 0x18);
			return MayContainConfiguredChildSlot(
				extra ? reinterpret_cast<const std::byte*>(extra) + sizeof(RE::BSExtraData) : nullptr,
				false,
				[&](std::uintptr_t keyword) { return settings.childLinkKeywordPointers.contains(keyword); });
		}

		[[nodiscard]] std::vector<RE::NiPointer<RE::TESObjectREFR>> SnapshotScriptParents(
			RE::BSScript::Internal::VirtualMachine& vm, const Configuration& settings)
		{
			// Exact OG/NG/AE layout and virtual-call evidence is recorded in
			// Docs/MANUAL_SELECTION_SCRIPT_SNAPSHOT.md. Never copy the engine's
			// small shared arrays or retain borrowed script/type pointers.
			static_assert(offsetof(RE::BSScript::Internal::VirtualMachine, attachedScriptsLock) == 0xBDF8);
			static_assert(offsetof(RE::BSScript::Internal::VirtualMachine, attachedScripts) == 0xBE00);
			static_assert(sizeof(decltype(vm.attachedScripts)) == 0x30);
			static_assert(sizeof(RE::BSTSmallSharedArray<RE::BSScript::Internal::AttachedScript>) == 0x10);
			static_assert(offsetof(RE::BSScript::Object, type) == 0x08);
			static_assert(offsetof(RE::BSScript::ObjectTypeInfo, name) == 0x10);
			static_assert(offsetof(RE::BSScript::ObjectTypeInfo, parentTypeInfo) == 0x18);

			std::vector<RE::BSFixedString> names;
			for (const auto& name : settings.parentScripts) {
				names.emplace_back(name.c_str());
			}
			std::vector<std::uint64_t> candidates;
			{
				const RE::BSAutoLock lock{ vm.attachedScriptsLock };
				candidates = CollectAttachedScriptCandidates(vm.attachedScripts, names,
					[](const auto& object) -> const RE::BSScript::ObjectTypeInfo* {
						return object ? object->type.get() : nullptr;
					},
					[](const auto* type) -> const RE::BSFixedString& { return type->name; },
					[](const auto* type) -> const RE::BSScript::ObjectTypeInfo* { return type->parentTypeInfo.get(); });
			}

			// Resolve through the engine handle policy, which also accepts ACHR
			// for ObjectReference. Do not truncate handles to FormIDs or require
			// loaded 3D, enabled state, a nearby cell, or a reverse child link.
			const auto& handles = vm.GetObjectHandlePolicy();
			std::vector<RE::NiPointer<RE::TESObjectREFR>> resolved;
			resolved.reserve(candidates.size());
			for (const auto handle : candidates) {
				auto* reference = static_cast<RE::TESObjectREFR*>(
					handles.GetObjectForHandle(RE::BSScript::GetVMTypeID<RE::TESObjectREFR>(), handle));
				if (reference) {
					resolved.emplace_back(reference);
				}
			}

			// Preserve the former represented REFR/ACHR boundary. Retain only
			// matching live form-map entries, without walking the entire map.
			// All VM/handle calls and final releases stay outside its read lock.
			std::vector<RE::NiPointer<RE::TESObjectREFR>> parents;
			parents.reserve(resolved.size());
			{
				const auto [forms, lock] = RE::TESForm::GetAllForms();
				const RE::BSAutoReadLock readLock{ lock };
				if (forms) {
					for (const auto& reference : resolved) {
						const auto found = forms->find(reference->formID);
						if (found != forms->end() && found->second == reference.get() &&
							reference->Is(RE::ENUM_FORM_ID::kREFR, RE::ENUM_FORM_ID::kACHR)) {
							parents.push_back(reference);
						}
					}
				}
			}
			return parents;
		}

		void BuildChildIndex(ScanState& state)
		{
			if (state.indexed) {
				return;
			}
			state.indexed = true;
			const auto& settings = *state.configuration;
			if (settings.childLinkKeywords.empty() || settings.parentScripts.empty()) {
				return;
			}
			const auto* gameVM = RE::GameVM::GetSingleton();
			const auto vm = gameVM ? gameVM->GetVM() : nullptr;
			if (!vm) {
				logger::warn("Optional parent-slot filtering could not inspect this scan: Papyrus VM unavailable");
				return;
			}

			// GameVM owns Internal::VirtualMachine; GetVM exposes its primary
			// IVirtualMachine base. No independent VM implementation is created.
			auto parents = SnapshotScriptParents(static_cast<RE::BSScript::Internal::VirtualMachine&>(*vm), settings);
			// Candidates cover the VM's entire attachment map. Recheck the
			// actual script and outgoing link now, after releasing the VM lock.
			state.childLinks.Build(parents, settings.childLinkKeywords,
				[&](const auto& parent) {
					if (!MayHaveConfiguredChildSlot(parent.get(), settings)) {
						return false;
					}
					const bool allowed = HasParentScript(*vm, parent.get(), settings.parentScripts);
					return allowed;
				},
				[](const auto& parent, RE::BGSKeyword* keyword) -> std::optional<std::uint32_t> {
					auto* child = EngineAPI::GetLinkedRef(parent.get(), keyword);
					if (child && child != parent.get()) {
						return child->formID;
					}
					return std::nullopt;
				});
			parents.clear();
		}
	}

	void Configure(
		const std::vector<std::string>& referenceKeywords,
		const std::vector<std::string>& scolFamilyRoots,
		const std::vector<std::string>& childLinkKeywords,
		const std::vector<std::string>& parentScripts)
	{
		auto next = std::make_shared<Configuration>();
		next->referenceKeywords = ResolveKeywords(referenceKeywords, "reference keyword");
		next->childLinkKeywords = ResolveKeywords(childLinkKeywords, "parent slot");
		for (auto* keyword : next->childLinkKeywords) {
			next->childLinkKeywordPointers.insert(reinterpret_cast<std::uintptr_t>(keyword));
		}
		auto* data = RE::TESDataHandler::GetSingleton();
		for (const auto& entry : scolFamilyRoots) {
			auto name = Trim(entry);
			if (name.empty()) {
				continue;
			}
			const auto* file = data ? data->LookupModByName(name) : nullptr;
			if (file && file->IsActive()) {
				AppendUnique(next->scolFamilyRoots, std::move(name));
			} else {
				CLIPBOARD_DEBUG_LOG(logger::info("Optional filter SCOL family skipped unavailable plugin: {}", name));
			}
		}
		for (const auto& entry : parentScripts) {
			AppendUnique(next->parentScripts, Trim(entry));
		}
		CLIPBOARD_DEBUG_LOG(logger::info("Optional filters configured: {} reference keywords, {} SCOL families, {} parent slots, {} parent scripts",
			next->referenceKeywords.size(), next->scolFamilyRoots.size(), next->childLinkKeywords.size(), next->parentScripts.size()));
		{
			const std::lock_guard lock{ configurationMutex };
			configuration = std::move(next);
		}
	}

	ScopedScan::ScopedScan()
	{
		if (scanDepth == 0) {
			auto next = std::make_unique<ScanState>();
			{
				const std::lock_guard lock{ configurationMutex };
				next->configuration = configuration;
			}
			scan = std::move(next);
		}
		++scanDepth;
	}

	ScopedScan::~ScopedScan()
	{
		if (--scanDepth == 0) {
			scan.reset();
		}
	}

	bool IsBlockedCollection(const RE::TESForm* baseForm)
	{
		if (!baseForm || !baseForm->Is(RE::ENUM_FORM_ID::kSCOL)) {
			return false;
		}
		const ScopedScan scope;
		return Policy::MatchesPluginFamily(baseForm->GetFile(0), scan->configuration->scolFamilyRoots);
	}

	bool IsBlocked(const RE::TESObjectREFR* reference)
	{
		if (!reference) {
			return false;
		}
		const ScopedScan scope;
		if (IsBlockedCollection(reference->data.objectReference)) {
			CLIPBOARD_DEBUG_LOG(logger::info("Optional filter rejected reference {:08X}: SCOL base {:08X} in configured plugin family",
				reference->formID, reference->data.objectReference->formID));
			return true;
		}
		for (auto* keyword : scan->configuration->referenceKeywords) {
			// Full IKeywordFormBase dispatch merges base/instance evidence with
			// reference-added/removed keywords. HasKeywordHelper alone does not.
			if (reference->HasKeyword(keyword, nullptr)) {
				CLIPBOARD_DEBUG_LOG(logger::info("Optional filter rejected reference {:08X}: reference keyword {:08X}",
					reference->formID, keyword->formID));
				return true;
			}
		}
		BuildChildIndex(*scan);
		return scan->childLinks.Matches(reference->formID,
			[&](const auto& parent, RE::BGSKeyword* keyword) {
				// A script may have changed the link since the snapshot. Membership
				// only blocks while the retained parent still points to this child.
				if (EngineAPI::GetLinkedRef(parent.get(), keyword) == reference) {
					CLIPBOARD_DEBUG_LOG(logger::info("Optional filter rejected reference {:08X}: attached-script parent {:08X} links through slot {:08X}",
						reference->formID, parent->formID, keyword->formID));
					return true;
				}
				return false;
			});
	}
}
