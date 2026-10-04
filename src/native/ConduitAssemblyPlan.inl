// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
// Included in ConduitConnections.cpp after the graph predicates and trace helpers.
class AssemblyPlan
{
public:
	AssemblyPlan(std::uint32_t toolID, RE::TESObjectREFR* owner, std::vector<std::uint32_t> rowIDs) :
		tool(toolID), workshop(owner), rows(std::move(rowIDs)) {}
	AssemblyRecords records;
	const std::uint32_t tool;
	RE::NiPointer<RE::TESObjectREFR> workshop;
	std::vector<std::uint32_t> rows;
	// One hold per distinct endpoint, not per edge. Never acquire thousands of
	// holds on the same workshop/endpoint (the engine reference counter is narrow).
	std::unordered_map<std::uint32_t, RE::NiPointer<RE::TESObjectREFR>> references;
	std::atomic<bool> cancelled{};
	std::size_t preChecked{}, finalChecked{}, failures{}, details{};
};

std::shared_ptr<AssemblyPlan> CreateAssemblyPlan(std::uint32_t tool, RE::TESObjectREFR* workshop,
	std::vector<std::uint32_t> rows)
{
	if (!CheckTaskContext("assembly creation")) { return nullptr; }
	if (!tool || !IsValidReference(workshop)) { return nullptr; }
	// A session may be dropped by a VM cancellation/load callback. Keep the
	// engine's intrusive references alive until their deletion runs on a task.
	return std::shared_ptr<AssemblyPlan>(new AssemblyPlan(tool, workshop, std::move(rows)),
		[](AssemblyPlan* plan) { EngineTaskDispatch::Retire(plan); });
}

bool ClaimAssemblyPlan(const std::shared_ptr<AssemblyPlan>& plan, std::uint32_t tool,
	RE::TESObjectREFR* workshop, const std::vector<std::uint32_t>& rows)
{
	if (!CheckTaskContext("assembly claim")) { return false; }
	if (!plan || plan->cancelled.load() || plan->tool != tool || !IsValidReference(workshop) ||
		plan->workshop.get() != workshop || plan->rows != rows || !plan->records.Claim()) { return false; }
	plan->finalChecked = 0;
	return true;
}

void CancelAssemblyPlan(const std::shared_ptr<AssemblyPlan>& plan) noexcept
{
	// Cancellation can arrive from outside the task thread; do not clear its containers.
	if (plan) { plan->cancelled.store(true); }
}

bool IsAssemblyPlanCancelled(const std::shared_ptr<AssemblyPlan>& plan) noexcept
{
	return plan && plan->cancelled.load();
}

void SealAssemblyPlan(const std::shared_ptr<AssemblyPlan>& plan, bool success) noexcept
{
	if (plan) { plan->records.Seal(success && !plan->cancelled.load()); }
}

namespace
{
	bool PlanCancelled(const std::shared_ptr<AssemblyPlan>& plan) noexcept
	{
		return plan && plan->cancelled.load();
	}
	void PlanFailure(Batch& batch, std::size_t index, const char* phase, const char* reason)
	{
		CheckCancellation(batch);
		auto& plan = *batch.assemblyPlan;
		if (!plan.records.MarkFailed(index)) { return; }
		++plan.failures;
		++batch.counts.failed;
		if (plan.details++ < 32) {
			const auto& pair = plan.records.pairs[index];
			logger::warn("Conduit assembly validation phase={} source={:08X} target={:08X} reason={}",
				phase, pair.source, pair.target, reason);
		}
	}
	RE::TESObjectREFR* PlannedReference(const AssemblyPlan& plan, std::uint32_t id)
	{
		const auto found = plan.references.find(id);
		return found == plan.references.end() ? nullptr : found->second.get();
	}
	bool PlanEndpointsValid(const AssemblyPlan& plan, const AssemblyRecords::Pair& pair,
		RE::TESObjectREFR* workshop, RE::BGSKeyword* keyword, const Batch& batch)
	{
		CheckCancellation(batch);
		auto* source = PlannedReference(plan, pair.source);
		auto* target = PlannedReference(plan, pair.target);
		if (!keyword || !IsValidReference(source) || !IsValidReference(target) ||
			source->formID != pair.source || target->formID != pair.target ||
			batch.excludedReferences.contains(pair.source) || batch.excludedReferences.contains(pair.target)) { return false; }
		CheckCancellation(batch);
		if (EngineAPI::GetLinkedRef(source, keyword) != workshop) { return false; }
		CheckCancellation(batch);
		return EngineAPI::GetLinkedRef(target, keyword) == workshop;
	}
	Progress RefreshPlannedReference(RE::TESObjectREFR* source, RE::BGSKeyword* keyword,
		RE::TESObjectREFR* workshop, RE::Workshop::ExtraData* data, Batch& batch,
		ReferenceCursor& cursor, std::size_t budget)
	{
		CheckCancellation(batch);
		const auto plan = batch.assemblyPlan;
		if (!plan || !plan->records.Claimed() || plan->workshop.get() != workshop) {
			++batch.counts.failed; cursor.complete = true; return Progress::Complete;
		}
		const auto* indices = plan->records.ForSource(source->formID);
		if (!indices) { ++batch.counts.ineligible; cursor.complete = true; return Progress::Complete; }
		if (!cursor.candidate) { cursor.candidate = true; ++batch.counts.candidates; }
		std::size_t visits{};
		while (cursor.nextPoint < indices->size() && visits++ < budget) {
			CheckCancellation(batch);
			const auto index = (*indices)[cursor.nextPoint];
			const auto& pair = plan->records.pairs[index];
			if (!PlanEndpointsValid(*plan, pair, workshop, keyword, batch)) {
				PlanFailure(batch, index, "refresh", "endpoint-invalid-or-owner-changed"); ++cursor.nextPoint; continue;
			}
			auto* target = PlannedReference(*plan, pair.target);
			if (!source->parentCell || !source->parentCell->GetbhkWorld() || !target->Get3D() ||
				!target->parentCell || !target->parentCell->GetbhkWorld()) {
				++batch.counts.pending; return Progress::Pending;
			}
			++cursor.nextPoint;
			++batch.counts.points; ++batch.counts.matches;
			const auto bits = TraceBits(*data, pair.source, pair.target, batch);
			if (batch.trace) {
				batch.trace->Safely([&] { batch.trace->MatchPair(TraceEndpoint(source), TraceEndpoint(target),
					false, pair.point, pair.position, bits); });
			}
			if (bits != 7) { PlanFailure(batch, index, "refresh", "intended-direct-edge-incomplete"); continue; }
			++batch.counts.existing;
			TraceRefresh(batch, target, data, Trace::Operation::Target);
			++batch.counts.refreshed;
		}
		if (cursor.nextPoint < indices->size()) { return Progress::InProgress; }
		// Preserve the original source refresh for every assembly candidate,
		// including candidates with no accepted edges.
		TraceRefresh(batch, source, data, Trace::Operation::Source);
		++batch.counts.refreshed;
		cursor.complete = true;
		return Progress::Complete;
	}
}

bool ValidateAssemblyPlan(RE::TESObjectREFR* workshop, RE::BGSKeyword* keyword,
	Batch& batch, std::size_t& cursor, const char* phase) try
{
	if (!CheckTaskContext("assembly validation")) { ++batch.counts.failed; return true; }
	CheckCancellation(batch);
	const auto plan = batch.assemblyPlan;
	if (!plan || !IsValidReference(workshop) || plan->workshop.get() != workshop || !keyword) {
		++batch.counts.failed; return true;
	}
	const auto* data = workshop->extraList ? workshop->extraList->GetByType<RE::Workshop::ExtraData>() : nullptr;
	const auto start = std::chrono::steady_clock::now();
	std::size_t visits{};
	while (cursor < plan->records.pairs.size() && visits++ < 64) {
		CheckCancellation(batch);
		const auto index = cursor;
		const auto& pair = plan->records.pairs[index];
		if (!PlanEndpointsValid(*plan, pair, workshop, keyword, batch)) {
			PlanFailure(batch, index, phase, "endpoint-invalid-or-owner-changed");
		} else if (!data || !HasCompleteEdge(*data, pair.source, pair.target, batch)) {
			PlanFailure(batch, index, phase, "intended-direct-edge-incomplete");
		}
		CheckCancellation(batch);
		++cursor;
		if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(1)) { break; }
	}
	if (std::string_view(phase) == "pre-refresh") { plan->preChecked = cursor; }
	else { plan->finalChecked = cursor; }
	return cursor == plan->records.pairs.size();
}
catch (const CancellationObserved&) {
	return false;
}

void LogAssemblyPlan(const Batch& batch, const char* phase)
{
	if (!batch.assemblyPlan) { return; }
	const auto& plan = *batch.assemblyPlan;
	CLIPBOARD_DEBUG_LOG(logger::info("Conduit assembly plan phase={} pairs={} ready={} claimed={} preChecked={} finalChecked={} failedPairs={} failureDetailsOmitted={} cancelled={} refreshDiscovery=frozen-assembly",
		phase, plan.records.pairs.size(), plan.records.Ready(), plan.records.Claimed(), plan.preChecked,
		plan.finalChecked, plan.failures, plan.details > 32 ? plan.details - 32 : 0, plan.cancelled.load()));
}
