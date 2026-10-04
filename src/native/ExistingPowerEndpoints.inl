// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
// Included after the native pattern, filtering and workshop-boundary helpers.
#include <unordered_map>
#include <unordered_set>
namespace
{
	namespace ExistingPower = Clipboard::ExistingPower;
	struct ExistingPowerExport
	{
		std::vector<ExistingPower::Endpoint> endpoints;
		std::vector<ExistingPower::Wire> wires;
		std::vector<NiPointer<TESObjectREFR>> retained;
	};

	bool IsStableExistingReference(const TESObjectREFR* reference)
	{
		return reference && !reference->IsCreated() && (reference->formID >> 24) != 0xFF &&
			reference->GetFile(0);
	}
	bool ExistingEndpointPolicy(TESObjectREFR* reference)
	{
		auto* base = reference ? reference->data.objectReference : nullptr;
		// These references are not selected or placed. Selection/import exclusions
		// must not drop an otherwise valid wire to an omitted object.
		return base && base->As<TESBoundObject>() && !IsWorkshop(base) && !base->As<BGSBendableSpline>() &&
			!istarts_with(GetPluginName(base).c_str(), "Clipboard.");
	}
	bool IsCreatedExistingReference(const TESObjectREFR* reference)
	{
		return reference && reference->IsCreated() && (reference->formID >> 24) == 0xFF;
	}
	bool ExistingEndpointInside(TESObjectREFR* endpoint, TESObjectREFR* workshop)
	{
		return endpoint && IsPositionWithinWorkshop(nullptr, workshop, endpoint->parentCell,
			endpoint->data.location.x, endpoint->data.location.y, endpoint->data.location.z);
	}
	bool ExistingEndpointOwnerAllowed(TESObjectREFR* endpoint, TESObjectREFR* workshop)
	{
		auto* owner = endpoint && workshopItemKeyword ? GetLinkedRef_Native(endpoint, workshopItemKeyword) : nullptr;
		return !owner || owner == workshop;
	}
	std::optional<ExistingPower::Form> RecordExistingForm(const TESForm* form, VMArray<BSFixedString>& plugins)
	{
		if (!form || !form->formID || (form->formID >> 24) == 0xFF || !form->GetFile(0)) { return {}; }
		int index = GetSelectedPluginIndex(form, plugins);
		if (index < 0) {
			auto name = GetPluginName(form);
			index = static_cast<int>(plugins.Length());
			plugins.Push(&name);
		}
		return ExistingPower::Form{ static_cast<UInt32>(index), GetLowerFormId(form) };
	}

	ExistingPowerExport CollectExistingPower(
		TESObjectREFR* workshop, const VMArray<TESObjectREFR*>& selected, VMArray<BSFixedString>& plugins)
	{
		ExistingPowerExport result;
		std::unordered_set<UInt32> selectedIds, visitedWires;
		std::unordered_map<UInt32, UInt32> endpointRows;
		for (auto* object : selected) { if (object) { selectedIds.insert(object->formID); } }
		const auto* spline = GetDefaultForm<BGSBendableSpline>("WorkshopSplineObject");
		if (!spline || !workshop) { return result; }
		for (UInt32 row = 0; row < selected.Length(); ++row) {
			auto* object = selected[row];
			auto* links = object && object->extraList ? object->extraList->GetByType<ExtraPowerLinks>() : nullptr;
			if (!links) { continue; }
			for (const auto& link : links->powerLinks) {
				auto* form = LookupFormByID(link.formID);
				NiPointer<TESObjectREFR> wire{ form ? form->As<TESObjectREFR>() : nullptr };
				if (!wire || wire->IsDeleted() || wire->data.objectReference != spline) { continue; }
				auto* ends = wire->extraList ? wire->extraList->GetByType<ExtraPowerLinks>() : nullptr;
				if (!ends || ends->powerLinks.size() != 2) { continue; }
				const auto a = ends->powerLinks[0].formID, b = ends->powerLinks[1].formID;
				const auto other = ExistingPower::OtherEndpoint(object->formID, a, b);
				if (!other) { continue; }
				const auto otherId = *other;
				if (selectedIds.contains(otherId) || !visitedWires.insert(wire->formID).second) { continue; }
				form = LookupFormByID(otherId);
				NiPointer<TESObjectREFR> endpoint{ form ? form->As<TESObjectREFR>() : nullptr };
				const ExistingPower::Eligibility eligibility{
					IsStableExistingReference(endpoint.get()) || IsCreatedExistingReference(endpoint.get()),
					endpoint && !(endpoint->GetFormFlags() & ((1u << 5) | (1u << 11))),
					true, endpoint && endpoint->parentCell, true,
					ExistingEndpointInside(endpoint.get(), workshop), ExistingEndpointOwnerAllowed(endpoint.get(), workshop),
					ExistingEndpointPolicy(endpoint.get()), true }; // Export does not require loaded 3D.
				if (const auto* reason = ExistingPower::Reject(eligibility)) {
					logger::warn("External wire export omitted wire {:08X}, selected row {}, endpoint {:08X}: {}",
						wire->formID, row, otherId, reason);
					continue;
				}
				auto found = endpointRows.find(otherId);
				if (found == endpointRows.end()) {
					if (result.endpoints.size() == ExistingPower::kMaximumEndpoints) {
						throw std::runtime_error("External endpoint export limit exceeded; original pattern retained");
					}
					const bool created = IsCreatedExistingReference(endpoint.get());
					const auto referenceKey = created ? std::optional<ExistingPower::Form>{ {0, endpoint->formID} } :
						RecordExistingForm(endpoint.get(), plugins);
					const auto baseKey = RecordExistingForm(endpoint->data.objectReference, plugins);
					const auto cellKey = RecordExistingForm(endpoint->parentCell, plugins);
					const auto workshopKey = RecordExistingForm(workshop, plugins);
					if (!referenceKey || !baseKey || !cellKey || !workshopKey) {
						logger::warn("External wire export omitted endpoint {:08X}: unavailable stable plugin identity", otherId);
						continue;
					}
					const auto index = static_cast<UInt32>(result.endpoints.size());
					result.endpoints.push_back({ *referenceKey, *baseKey, *cellKey, *workshopKey, created,
						{ endpoint->data.location.x, endpoint->data.location.y, endpoint->data.location.z } });
					result.retained.push_back(std::move(endpoint));
					found = endpointRows.emplace(otherId, index).first;
				}
				if (result.wires.size() == ExistingPower::kMaximumWires) {
					throw std::runtime_error("External wire export limit exceeded; original pattern retained");
				}
				result.wires.push_back({ row, found->second });
			}
		}
		CLIPBOARD_DEBUG_LOG(logger::info("External power export: {} existing endpoints, {} wires, {} placement rows",
			result.endpoints.size(), result.wires.size(), selected.Length()));
		return result;
	}

	TESForm* ResolveExistingForm(const ExistingPower::Form& key, const VMArray<BSFixedString>& plugins)
	{
		if (key.plugin >= plugins.Length() || !key.local) { return nullptr; }
		auto* data = TESDataHandler::GetSingleton();
		const auto* file = data ? data->LookupModByName(plugins[key.plugin].c_str()) : nullptr;
		if (!file || !file->IsActive() || !ExistingPower::LocalIdFits(key.local, file->IsLight())) { return nullptr; }
		const auto full = file->IsLight() ? 0xFE000000u | (static_cast<UInt32>(file->GetSmallFileCompileIndex()) << 12) | key.local :
			(static_cast<UInt32>(file->GetCompileIndex()) << 24) | key.local;
		// Never use the ordinary placement resolver's bottlecap substitution here.
		return LookupFormByID(full);
	}

	struct ExistingPowerOrigin { UInt32 cell; std::array<double, 3> position; };
	std::optional<ExistingPowerOrigin> ReadExistingPowerOrigin(UInt32 slot, const VMArray<BSFixedString>& plugins)
	{
		auto origin = GetPatternReferenceInformation(nullptr, slot);
		UInt32 cell{}, plugin{};
		float x{}, y{}, z{};
		if (!origin || !origin->Get("cellId", &cell) || !origin->Get("cellPlugin", &plugin) ||
			!origin->Get("positionX", &x) || !origin->Get("positionY", &y) || !origin->Get("positionZ", &z)) { return {}; }
		auto* sourceCell = ResolveExistingForm({plugin, cell}, plugins);
		if (!sourceCell || !sourceCell->As<TESObjectCELL>()) { return {}; }
		return ExistingPowerOrigin{ sourceCell->formID, {x, y, z} };
	}
	bool AtExistingPowerOrigin(TESObjectREFR* tool, const std::optional<ExistingPowerOrigin>& origin)
	{
		return tool && !tool->IsDeleted() && tool->parentCell && origin && tool->parentCell->formID == origin->cell &&
			ExistingPower::SamePosition(origin->position, {tool->data.location.x, tool->data.location.y, tool->data.location.z});
	}

}

// The warning denominator excludes external wires intentionally ignored at a
// relocated tool. Actual wire rows still win over understated legacy metadata.
UInt32 GetPatternImportWireCount(StaticFunctionTag*, TESObjectREFR* tool, UInt32 slot)
{
	const auto internal = GetPatternWires(nullptr, slot).Length();
	std::ifstream file(GetPatternFilePath(slot));
	const auto external = ExistingPower::Read(file);
	UInt32 declared{};
	auto general = GetPatternGeneralInformation(nullptr, slot);
	general.Get("wireCount", &declared);
	if (external.present && !AtExistingPowerOrigin(tool, ReadExistingPowerOrigin(slot, GetPatternPlugins(nullptr, slot)))) { return internal; }
	return std::max(declared, internal + static_cast<UInt32>(external.wires.size()));
}
