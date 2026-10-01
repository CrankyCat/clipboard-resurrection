#pragma once

#include "LegacyCompat.h"
#include "ScrapTargets.h"

#include <functional>
#include <memory>
#include <tuple>

#include "F4SEObjectInterface.h"

namespace Clipboard::Latent
{
	using ReferenceArray = VMArray<RE::TESObjectREFR*>;
	using ScrapBatch = Scrapping::Batch<RE::NiPointer<RE::TESObjectREFR>>;

	// Construction, slices, cancellation and destruction all belong to the
	// engine task queue. A slice performs at most one complete wire transaction
	// or one bounded conduit scan; the latent coordinator only consumes results.
	class PowerWorkJob
	{
	public:
		virtual ~PowerWorkJob() = default;
		virtual bool RunSlice(ReferenceArray& result) = 0;
		virtual void Cancel() noexcept = 0;
	};
	using PowerCancellation = std::function<bool()>;

	struct Callbacks
	{
		ReferenceArray (*createSelectionBox)(
			std::uint32_t,
			StaticFunctionTag*,
			RE::TESObjectREFR*,
			RE::TESForm*,
			std::uint32_t,
			std::uint32_t,
			std::uint32_t){};
		void (*scrapSelection)(std::uint32_t, StaticFunctionTag*, RE::TESObjectREFR*){};
		void (*scrapObjects)(std::uint32_t, StaticFunctionTag*, ReferenceArray){};
		void (*sendWorkshopEvent)(std::uint32_t, StaticFunctionTag*, RE::TESObjectREFR*, RE::BSFixedString){};
		void (*enableObjects)(std::uint32_t, StaticFunctionTag*, ReferenceArray){};
		void (*disableObjects)(std::uint32_t, StaticFunctionTag*, ReferenceArray){};
		void (*scaleSelection)(std::uint32_t, StaticFunctionTag*, RE::TESObjectREFR*, float, bool){};
		std::shared_ptr<PowerWorkJob> (*createTransmitPowerJob)(
			std::uint32_t, RE::TESObjectREFR*, PowerCancellation){};
		ReferenceArray (*pastePatternObjects)(std::uint32_t, StaticFunctionTag*, RE::TESObjectREFR*, std::uint32_t){};
		std::shared_ptr<PowerWorkJob> (*createPatternWireJob)(
			std::uint32_t,
			RE::TESObjectREFR*,
			std::uint32_t,
			ReferenceArray,
			bool,
			PowerCancellation){};
		ScrapBatch (*snapshotScrap)(const ReferenceArray&){};
		bool (*scrapTarget)(std::uint32_t, RE::TESObjectREFR*, RE::TESObjectREFR*){};
		bool (*useWorkshopThrottling)(){};
		VMArray<ImportPlacementRow> (*pastePatternObjectsWithReuse)(std::uint32_t, StaticFunctionTag*, RE::TESObjectREFR*, std::uint32_t){};
		bool (*enableWorkshopWaitSnapshots)(){};
		bool (*useSingleWorkshopCallback)(){};
		std::uint32_t (*workshopCallbackSpacingMs)(){};
		bool (*enableWorkshopEarlyWaitSnapshot)(){};
		std::int32_t (*workshopCallbackLimit)(){};
		std::int32_t (*workshopCallbackResumeAt)(){};
		bool (*tryScaleSelection)(std::uint32_t, StaticFunctionTag*, RE::TESObjectREFR*, float, bool){};
	};

	// Registers eleven distinct F4SE factories and binds the eleven latent methods on
	// ClipboardExtension. Returns false without binding methods if the Object
	// interface or callback table is incomplete.
	[[nodiscard]] bool Register(RE::BSScript::IVirtualMachine& a_vm, const Callbacks& a_callbacks);

	// Additive, serialized bounded jobs. Existing eleven functor names/versions
	// remain intact for older co-saves and external Papyrus callers.
	[[nodiscard]] ReferenceArray GetSuccessfulImportRows(StaticFunctionTag*, ReferenceArray sources, ReferenceArray originals,
		VMArray<std::int32_t> result, bool workshop);
	[[nodiscard]] bool EnqueuePrepareImportedRows(RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate,
		RE::TESObjectREFR*, ReferenceArray, bool);
	[[nodiscard]] bool EnqueueInitializeImportedWorkshopRows(RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate,
		RE::TESObjectREFR*, ReferenceArray);
	[[nodiscard]] bool EnqueueReconnectImportedPowerForRows(RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate,
		RE::TESObjectREFR*, ReferenceArray, ReferenceArray);
	[[nodiscard]] bool EnqueueConnectImportedPowerForRows(RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate,
		RE::TESObjectREFR*, ReferenceArray, ReferenceArray);
	[[nodiscard]] bool EnqueueRefreshImportedPowerForRows(RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate,
		RE::TESObjectREFR*, ReferenceArray, ReferenceArray);
	[[nodiscard]] bool EnqueuePrepareImportedObjects(RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate,
		RE::TESObjectREFR*, ReferenceArray, bool);
	[[nodiscard]] bool EnqueueGetImportedPowerGenerators(RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate,
		ReferenceArray);
	[[nodiscard]] bool EnqueueReconnectImportedPower(RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate,
		RE::TESObjectREFR*, ReferenceArray);
	[[nodiscard]] bool EnqueueInitializeImportedWorkshopObjects(RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate,
		RE::TESObjectREFR*, ReferenceArray);
	[[nodiscard]] bool EnqueueScrapObjectsPaced(RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate,
		RE::TESObjectREFR*, ReferenceArray);
	[[nodiscard]] bool EnqueueGetImportedAnimationCandidates(RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate,
		RE::TESObjectREFR*, ReferenceArray);
	void CancelImportedObjects(StaticFunctionTag*, RE::TESObjectREFR*);
	std::int32_t BeginImportedPowerProgress(StaticFunctionTag*, RE::TESObjectREFR*, ReferenceArray);
	void AdvanceImportedPowerProgress(StaticFunctionTag*, RE::TESObjectREFR*, std::int32_t token, std::int32_t row);
	bool EndImportedPowerProgress(StaticFunctionTag*, RE::TESObjectREFR*, std::int32_t token);
	void ClearImportedProgress(StaticFunctionTag*, RE::TESObjectREFR*);
	void ResetImportJobs();
	// Exclusive maintenance token. All queued/running jobs and outstanding
	// workshop callbacks prevent entry; new latent work is refused while held.
	[[nodiscard]] std::int32_t BeginLegacyCleanup();
	[[nodiscard]] bool IsLegacyCleanupActive(std::int32_t token = 0);
	void EndLegacyCleanup(std::int32_t token);

	[[nodiscard]] bool EnqueueCreateSelectionBox(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_tool,
		RE::TESForm* a_wall,
		std::uint32_t a_xLength,
		std::uint32_t a_yLength,
		std::uint32_t a_zLength);
	[[nodiscard]] bool EnqueueScrapSelection(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_reference);
	[[nodiscard]] bool EnqueueScrapObjects(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		ReferenceArray a_references);
	[[nodiscard]] bool EnqueueSendWorkshopEvent(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_reference,
		RE::BSFixedString a_eventName);
	[[nodiscard]] bool EnqueueEnableObjects(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		ReferenceArray a_references);
	[[nodiscard]] bool EnqueueDisableObjects(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		ReferenceArray a_references);
	[[nodiscard]] bool EnqueueScaleSelection(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_reference,
		float a_scale,
		bool a_maintainShape);
	[[nodiscard]] bool EnqueueTryScaleSelection(RE::BSScript::IVirtualMachine&,
		std::uint32_t, std::monostate, RE::TESObjectREFR*, float, bool);
	[[nodiscard]] bool EnqueueTransmitPower(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_reference);
	[[nodiscard]] bool EnqueuePastePatternObjects(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_reference,
		std::uint32_t a_slot);
	[[nodiscard]] bool EnqueuePastePatternObjectsWithReuse(
		RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate, RE::TESObjectREFR*, std::uint32_t);
	[[nodiscard]] bool EnqueuePastePatternWires(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_reference,
		std::uint32_t a_slot);
	[[nodiscard]] bool EnqueuePastePatternWiresForRows(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_reference,
		std::uint32_t a_slot,
		ReferenceArray a_placedRows);
}
