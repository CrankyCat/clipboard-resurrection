// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
// Included inside Clipboard::Latent's private implementation namespace, after
// the existing VM-variable serializer and LatentFunctorBase definitions.

enum class ImportOperation : std::uint8_t { Prepare, Generators, Reconnect };
using ImportSummary = VMArray<std::int32_t>;

struct ImportLease { std::atomic<bool> cancelled{}; };
std::mutex g_importLock;
std::unordered_map<std::uint64_t, std::weak_ptr<ImportLease>> g_importLeases;

#include "ImportPowerProgress.inl"

void CancelImportTool(RE::TESObjectREFR* tool)
{
	if (!tool) { return; }
	ImportProgress::ClearPercentage(tool->formID);
	RE::BSScript::Variable variable;
	RE::BSScript::PackVariable(variable, tool);
	const auto handle = ImportToolHandle(variable);
	CancelPowerTasks(handle);
	std::scoped_lock lock(g_importLock);
	if (const auto progress = g_powerProgress.find(handle); progress != g_powerProgress.end()) {
		progress->second->Stop();
		g_powerProgress.erase(progress);
	}
	const auto found = g_importLeases.find(handle);
	if (found != g_importLeases.end()) {
		if (const auto lease = found->second.lock()) { lease->cancelled.store(true); }
	}
}

void ResetImportLeases()
{
	ResetPowerTasks();
	ImportProgress::ClearPercentage();
	std::scoped_lock lock(g_importLock);
	for (const auto& [handle, weak] : g_importLeases) {
		if (const auto lease = weak.lock()) { lease->cancelled.store(true); }
	}
	g_importLeases.clear();
	for (const auto& [handle, session] : g_powerProgress) { session->Stop(); }
	g_powerProgress.clear();
}


void LogExcludedImportRow(const char* stage, std::size_t index, RE::TESObjectREFR* ref, std::int32_t outcome) noexcept
{
	try {
		F4SE::log::warn("Import {} excluded physical row {} (zero-based) from subsequent wiring/power: outcome {}, ref {:08X}, base {:08X}",
			stage, index, outcome, ref ? ref->formID : 0, ref && ref->data.objectReference ? ref->data.objectReference->formID : 0);
	} catch (...) { /* Diagnostic failure cannot change a row outcome. */ }
}

template <ImportOperation Operation, const char* Name, bool RowResults = false,
	ConduitConnections::ExecutionMode PowerMode = ConduitConnections::ExecutionMode::Combined>
class ImportFunctor final : public LatentFunctorBase
{
public:
	explicit ImportFunctor(F4SE::SerializationTag tag) : LatentFunctorBase(tag) {}
	explicit ImportFunctor(std::uint32_t stackID, RE::TESObjectREFR* tool, ReferenceArray rows, bool disableHavok = false, ReferenceArray originals = {}) :
		LatentFunctorBase(stackID), _disableHavok(disableHavok)
	{
		if (rows.size() > ImportJobs::kMaximumRows) { throw std::length_error("Import row limit exceeded"); }
		RE::BSScript::PackVariable(_tool, tool);
		RE::BSScript::PackVariable(_references, std::move(rows));
		const auto array = RE::BSScript::get<RE::BSScript::Array>(_references);
		_state.rows.resize(array ? array->size() : 0);
		ImportCursorPayload::Resize<Operation == ImportOperation::Reconnect>(_cursors, _state.rows.size());
		if constexpr (RowResults && Operation == ImportOperation::Reconnect) {
			if (originals.size() != _state.rows.size()) { throw std::invalid_argument("Import power row maps differ in length"); }
			RE::BSScript::PackVariable(_originalReferences, std::move(originals));
		}
	}

	[[nodiscard]] const char* ClassName() const override { return Name; }
	[[nodiscard]] std::uint32_t ClassVersion() const override { return Operation == ImportOperation::Reconnect ? 2 : 1; }

	bool Save(const F4SE::SerializationInterface* intfc) override
	{
		try {
			if (_lease && _lease->cancelled.load() && !_state.cancelled) {
				_state.cancelled = true;
				_state.cursor = 0;
			}
			if (!SaveStack(intfc) || !WriteVariable(intfc, _tool) || !WriteVariable(intfc, _references) ||
				!WriteExact(intfc, _disableHavok) ||
				!_state.Save([&](const auto& value) { return WriteExact(intfc, value); })) { return false; }
			const auto& c = _connections.counts;
			for (const auto value : { c.references, c.candidates, c.points, c.matches, c.added, c.existing,
				c.pending, c.failed, c.ineligible, c.missingParents, c.rejectedEndpoints, c.duplicatePairs, c.refreshed,
				c.noReference, c.noSnapPoint, c.nonReferenceHit, c.unexpectedQueryStatus }) {
				if (!WriteExact(intfc, value)) { return false; }
			}
			if (!ImportCursorPayload::Save<Operation == ImportOperation::Reconnect>(_cursors, _state.rows.size(),
				[&](const auto& value) { return WriteExact(intfc, value); })) { return false; }
			// A partially scanned model restarts after load; the engine graph is
			// authoritative for duplicate suppression, never saved raw FormIDs.
			if constexpr (RowResults && Operation == ImportOperation::Reconnect) {
				if (!WriteVariable(intfc, _originalReferences)) { return false; }
			}
			return true;
		} catch (...) { return false; }
	}

	bool Load(const F4SE::SerializationInterface* intfc, std::uint32_t version) override
	{
		try {
			std::uint8_t disableHavok{};
			if ((version != 1 && version != ClassVersion()) || !LoadStack(intfc) || !ReadVariable(intfc, _tool) || !ReadVariable(intfc, _references) ||
				!ReadExact(intfc, disableHavok) || disableHavok > 1 ||
				!_state.Load([&](auto& value) { return ReadExact(intfc, value); })) { return false; }
			_disableHavok = disableHavok != 0;
			if (!_references.is<RE::BSScript::Array>()) { return false; }
			const auto array = RE::BSScript::get<RE::BSScript::Array>(_references);
			if (!array || array->size() != _state.rows.size()) { return false; }
			auto& c = _connections.counts;
			for (auto* value : { &c.references, &c.candidates, &c.points, &c.matches, &c.added, &c.existing,
				&c.pending, &c.failed, &c.ineligible, &c.missingParents, &c.rejectedEndpoints, &c.duplicatePairs, &c.refreshed,
				&c.noReference, &c.noSnapPoint, &c.nonReferenceHit, &c.unexpectedQueryStatus }) {
				if (!ReadExact(intfc, *value)) { return false; }
			}
			if constexpr (Operation == ImportOperation::Reconnect) {
				// Version 1 also counted each skipped named parent as a failure.
				// Keep the payload layout and migrate that diagnostic bookkeeping.
				if (version == 1 && !c.MigrateVersion1Failures()) { return false; }
			}
			if (!ImportCursorPayload::Load<Operation == ImportOperation::Reconnect>(_cursors, _state.rows.size(),
				[&](auto& value) { return ReadExact(intfc, value); })) { return false; }
			if constexpr (RowResults && Operation == ImportOperation::Reconnect) {
				if (version != ClassVersion() || !ReadVariable(intfc, _originalReferences) || !_originalReferences.is<RE::BSScript::Array>()) { return false; }
				const auto originals = RE::BSScript::get<RE::BSScript::Array>(_originalReferences);
				if (!originals || originals->size() != _state.rows.size()) { return false; }
				// The new scoped power operation never resumes post-load mutations.
				if (!_state.done) { _state.cancelled = true; _state.cursor = 0; }
			}
			return true;
		} catch (...) { return false; }
	}

	bool ShouldReschedule(std::int32_t& delayMilliseconds) override
	{
		delayMilliseconds = _delayMs;
		return !_state.done || _tracePending;
	}
	bool ShouldResumeStack(std::uint32_t& stackID) override
	{
		stackID = _stackID;
		return _state.done && !_tracePending;
	}

	std::uint64_t PowerToolHandle() const { return ImportToolHandle(_tool); }
	void SetPowerCancellation(std::function<bool()> test) { _connections.cancelled = std::move(test); }
	void AbortPowerTask() noexcept
	{
		if (_lease) { _lease->cancelled.store(true); }
		ConduitConnections::CancelAssemblyPlan(_connections.assemblyPlan);
		try { if (_powerProgress && _lease) { _powerProgress->ReleaseAssemblyMembership(); } } catch (...) {}
		_connections.assemblyPlan.reset();
		_lease.reset();
	}
	static void PowerFailureResult(RE::BSScript::Variable& result)
	{
		ImportSummary summary;
		for (const auto value : { -1, 0, 0, 0, 0, 0, 0, 1, 0 }) { summary.push_back(value); }
		RE::BSScript::PackVariable(result, std::move(summary));
	}
	void CopyPowerCheckpoint(ImportFunctor& target, bool initial)
	{
		// Called only after acquiring the published slice and before queuing the
		// next. Copy dirty rows only; never copy retained graph owners to the VM.
		target._stackID = _stackID;
		if (initial) {
			target._tool = _tool; target._references = _references;
			target._originalReferences = _originalReferences;
			target._disableHavok = _disableHavok;
			target._state = _state;
			target._cursors.resize(_cursors.size());
		} else {
			for (const auto index : _powerDirtyRows) {
				target._state.rows[index] = _state.rows[index];
				target._cursors[index].started = _cursors[index].started;
				target._cursors[index].candidate = _cursors[index].candidate;
			}
			target._state.cursor = _state.cursor; target._state.passes = _state.passes;
			target._state.elapsedMs = _state.elapsedMs; target._state.preparing = _state.preparing;
			target._state.cancelled = _state.cancelled;
			target._state.done = _state.done && !_tracePending;
		}
		target._connections.counts = _connections.counts;
		_powerDirtyRows.clear();
	}

	bool Run(RE::BSScript::Variable& result) override
	{
		using namespace ImportJobs;
		try {
			if constexpr (Operation == ImportOperation::Reconnect) {
				if (_connections.Cancelled()) { AbortPowerTask(); PowerFailureResult(result); _state.done = _state.cancelled = true; _tracePending = false; return true; }
			}
			const auto now = std::chrono::steady_clock::now();
			if (_lastRun != std::chrono::steady_clock::time_point{}) {
				const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - _lastRun).count();
				_state.elapsedMs += static_cast<std::uint32_t>(std::clamp<std::int64_t>(delta, 0, 60000));
			}
			_lastRun = now;
			if (!_started) {
				_started = true;
				if constexpr (RowResults && Operation == ImportOperation::Reconnect) {
					_powerProgress = FindPowerProgress(_tool);
				}
				if constexpr (RowResults && Operation == ImportOperation::Reconnect) {
					const auto rows = UnpackSafely<ReferenceArray>(_references);
					const auto originals = UnpackSafely<ReferenceArray>(_originalReferences);
					if (!rows || !originals || rows->size() != originals->size()) { _state.cancelled = true; }
					else {
						std::vector<std::uint32_t> originalIDs, successfulIDs;
						for (auto* ref : *originals) { originalIDs.push_back(ref ? ref->formID : 0); }
						for (auto* ref : *rows) { successfulIDs.push_back(ref && !ref->IsDeleted() ? ref->formID : 0); }
						_connections.excludedReferences = ImportRows::Excluded(originalIDs, successfulIDs);
					}
				}
				if constexpr (Operation == ImportOperation::Prepare) {
					_importNotice.Start(!g_callbacks.useWorkshopThrottling || g_callbacks.useWorkshopThrottling());
				}
				if constexpr (Operation != ImportOperation::Generators) {
					if (!AcquireLease()) {
						_state.cancelled = true;
						// A busy tool's rows can belong to the other active job.
						// Reject without touching those rows; the script also guards
						// the entire paste before placement. A missing tool instead
						// enters normal bounded visibility cleanup.
						_state.done = ImportToolHandle(_tool) != 0;
						if (!_state.done) { _state.cursor = 0; }
						F4SE::log::error("Import stage {} rejected: tool unavailable or another job owns it", Name);
					}
				}
				if constexpr (RowResults && Operation == ImportOperation::Reconnect) {
					// A rejected/cancelled job cannot choose another job's UI plan.
					if (_powerProgress && !_state.cancelled && !_state.done) {
						_powerProgress->SelectNetworkPlan(PowerMode == ConduitConnections::ExecutionMode::ConnectionsOnly);
					}
				}
			}
			auto* tool = UnpackSafely<RE::TESObjectREFR*>(_tool).value_or(nullptr);
			if constexpr (Operation != ImportOperation::Generators) {
				if (!tool || tool->IsDeleted() || (_lease && _lease->cancelled.load())) {
					if (!_state.cancelled) { _state.cursor = 0; }
					_state.cancelled = true;
				}
			}
			const auto array = RE::BSScript::get<RE::BSScript::Array>(_references);
			if (!array || array->size() != _state.rows.size()) { _state.cancelled = _state.done = true; }
			if constexpr (Operation == ImportOperation::Prepare) {
				if (_state.cancelled || _state.done) { _importNotice.Stop(); }
				else {
					_importNotice.Update([lease = std::weak_ptr<ImportLease>{ _lease }] {
						const auto active = lease.lock();
						return active && !active->cancelled.load();
					});
				}
			}
			_delayMs = 1;
			if constexpr (RowResults && Operation == ImportOperation::Reconnect && PowerMode != ConduitConnections::ExecutionMode::Combined) {
				if (!_planAttached && !_state.cancelled && !_state.done) {
					_planAttached = true;
					if (!AttachAssemblyPlan(tool)) {
						++_connections.counts.failed;
						_state.cancelled = true;
						_state.cursor = 0;
						F4SE::log::error("Import power {} cannot use assembly membership: missing, stale, incomplete or already claimed", Name);
					}
				}
				AttachPairTrace(tool);
				if (_connections.trace && _connections.trace->Active()) { ++_connections.trace->slice; }
				if (_state.cancelled && _connections.trace) { _connections.trace->Cancel(); }
				if (_state.cancelled) { ConduitConnections::CancelAssemblyPlan(_connections.assemblyPlan); }
				if constexpr (PowerMode == ConduitConnections::ExecutionMode::RefreshOnly) {
					if (!_planPreDone && !_state.cancelled) {
						_planPreDone = AssemblyBoundary(tool, _planPreCursor, "pre-refresh");
						if (!_planPreDone) { return true; }
					}
					if (!_tracePreDone && !_state.cancelled) {
						_tracePreDone = PairTraceBoundary(tool, ConduitConnections::Trace::Point::PreRefresh, _tracePreCursor);
						if (!_tracePreDone) { return true; }
					}
				}
			}
			std::uint32_t visits{};
			while (!_state.done && _state.cursor < _state.rows.size() && visits < kRowsPerSlice) {
				if constexpr (Operation == ImportOperation::Reconnect) {
					if (_connections.Cancelled()) { return true; }
					_powerDirtyRows.push_back(_state.cursor);
				}
				const auto index = _state.cursor;
				auto& row = _state.rows[index];
				if (row.stage == RowStage::Initial || row.stage == RowStage::Waiting) {
					auto* ref = UnpackSafely<RE::TESObjectREFR*>(array->elements[index]).value_or(nullptr);
					if (_state.cancelled) {
						// Leave already-created objects visible when interrupted. Never
						// continue Havok, animations or power work after cancellation.
						if constexpr (Operation == ImportOperation::Prepare) {
							if (ref && !ref->IsDeleted() && row.stage == RowStage::Initial) { EngineAPI::Enable(ref, false); }
						}
						row.stage = RowStage::Failed;
					} else if (!ref) {
						if (array->elements[index].is<std::nullptr_t>()) { row.stage = RowStage::Missing; }
						else { LogPreparationPending(nullptr, index, "reference-unavailable"); }
					} else if (ref->IsDeleted()) {
						row.stage = RowStage::Failed;
					} else {
						ProcessRow(tool, ref, index, row);
					}
				}
				++visits;
				if constexpr (RowResults && Operation == ImportOperation::Reconnect) {
					if (_powerProgress && !_state.cancelled && row.stage == RowStage::Complete) {
						constexpr auto unit = PowerMode == ConduitConnections::ExecutionMode::ConnectionsOnly ?
							ImportProgress::PowerUnits::Assembly : ImportProgress::PowerUnits::Network;
						_powerProgress->Complete(index, unit);
					}
				}
				// A large CPA model may need several slices of this same row.
				if constexpr (Operation == ImportOperation::Reconnect) {
					if (_pointWorkRemaining) { _pointWorkRemaining = false; break; }
				}
				++_state.cursor;
				const auto sliceMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - now).count();
				if (SliceComplete(visits, static_cast<std::uint64_t>(sliceMs))) { break; }
			}
			if (!_state.done && _state.cursor == _state.rows.size()) {
				if (_state.cancelled || !_state.HasPending()) {
					_state.done = true;
				} else if (_state.preparing) {
					_state.preparing = false;
					_state.cursor = 0;
				} else if (++_state.passes >= kMaximumPasses) {
					_state.done = true;
				} else {
					_state.cursor = 0;
					_delayMs = kRetryDelayMs;
				}
			}
			if (_state.done) {
				if constexpr (RowResults && Operation == ImportOperation::Reconnect && PowerMode != ConduitConnections::ExecutionMode::Combined) {
					if (!_state.cancelled) {
						_tracePending = !AssemblyBoundary(tool, _planEndCursor,
							PowerMode == ConduitConnections::ExecutionMode::ConnectionsOnly ? "assembly-end" : "final");
						if (_tracePending) { return true; }
					}
					constexpr auto point = PowerMode == ConduitConnections::ExecutionMode::ConnectionsOnly ?
						ConduitConnections::Trace::Point::AssemblyEnd : ConduitConnections::Trace::Point::Final;
					_tracePending = !_state.cancelled && !PairTraceBoundary(tool, point, _traceEndCursor);
					if (_tracePending) { return true; }
				}
				Finish(result);
			}
			return true;
		} catch (const std::exception& error) {
			try { F4SE::log::error("Import stage {} failed: {}", Name, error.what()); } catch (...) {}
		} catch (...) {
			try { F4SE::log::error("Import stage {} failed with an unknown exception", Name); } catch (...) {}
		}
		_importNotice.Stop();
		// Clean up Initial rows on subsequent bounded slices, rather than
		// abandoning invisible persistent placements after a C++ exception.
		_state.cancelled = true;
		_state.cursor = 0;
		_delayMs = 1;
		// A persistent allocation/engine-wrapper exception must not keep an
		// import stack suspended forever, even when visibility cleanup fails.
		if (++_cleanupErrors >= 3) { _state.done = true; }
		if (_state.done) { try { Finish(result); } catch (...) { result = nullptr; } }
		return true;
	}

private:
	bool AttachAssemblyPlan(RE::TESObjectREFR* tool)
	{
		if (!_lease || !_powerProgress || !tool || tool->IsDeleted()) { return false; }
		auto* object = RE::TESForm::GetFormByEditorID<RE::BGSDefaultObject>(RE::BSFixedString{ "WorkshopItem" });
		auto* keyword = object ? object->GetForm<RE::BGSKeyword>() : nullptr;
		auto* workshop = keyword ? EngineAPI::GetLinkedRef(tool, keyword) : nullptr;
		const auto rows = UnpackSafely<ReferenceArray>(_references);
		if (!workshop || workshop->IsDeleted() || !rows) { return false; }
		_connections.assemblyPlan = _powerProgress->AssemblyMembership(PowerMode == ConduitConnections::ExecutionMode::ConnectionsOnly,
			tool->formID, workshop, *rows);
		return _connections.assemblyPlan != nullptr;
	}
	bool AssemblyBoundary(RE::TESObjectREFR* tool, std::size_t& cursor, const char* phase)
	{
		auto* object = RE::TESForm::GetFormByEditorID<RE::BGSDefaultObject>(RE::BSFixedString{ "WorkshopItem" });
		auto* keyword = object ? object->GetForm<RE::BGSKeyword>() : nullptr;
		auto* workshop = keyword && tool ? EngineAPI::GetLinkedRef(tool, keyword) : nullptr;
		return ConduitConnections::ValidateAssemblyPlan(workshop, keyword, _connections, cursor, phase);
	}
	void AttachPairTrace(RE::TESObjectREFR* tool) noexcept
	{
		if (_traceAttached || _state.cancelled || _state.done || !_lease || !_powerProgress || !tool || tool->IsDeleted()) { return; }
		_traceAttached = true;
		// Decide once for this job, before any diagnostic engine lookups or
		// unpacking the full row array. A late logging toggle must not attach a
		// partial trace and present it as complete assembly/refresh coverage.
		if (!Logging::Enabled()) { return; }
		try {
			auto* object = RE::TESForm::GetFormByEditorID<RE::BGSDefaultObject>(RE::BSFixedString{ "WorkshopItem" });
			auto* keyword = object ? object->GetForm<RE::BGSKeyword>() : nullptr;
			auto* workshop = keyword ? EngineAPI::GetLinkedRef(tool, keyword) : nullptr;
			const auto rows = UnpackSafely<ReferenceArray>(_references);
			if (!workshop || workshop->IsDeleted() || !rows) { return; }
			_connections.trace = _powerProgress->PairTrace(PowerMode == ConduitConnections::ExecutionMode::ConnectionsOnly,
				tool->formID, workshop->formID, _stackID, *rows);
		} catch (...) { /* A diagnostic must never fail an import. */ }
	}
	bool PairTraceBoundary(RE::TESObjectREFR* tool, ConduitConnections::Trace::Point point, std::size_t& cursor) noexcept
	{
		if (!_connections.trace || !_connections.trace->Active()) { return true; }
		try {
			auto* object = RE::TESForm::GetFormByEditorID<RE::BGSDefaultObject>(RE::BSFixedString{ "WorkshopItem" });
			auto* keyword = object ? object->GetForm<RE::BGSKeyword>() : nullptr;
			auto* workshop = keyword && tool ? EngineAPI::GetLinkedRef(tool, keyword) : nullptr;
			return ConduitConnections::TraceBoundary(workshop, _connections, point, cursor);
		} catch (...) { _connections.trace->error = true; return true; }
	}
	void LogPreparationPending(RE::TESObjectREFR* ref, std::size_t index, const char* reason) const noexcept
	{
		if constexpr (Operation == ImportOperation::Prepare) {
			// Record the actual blocking branch on the last readiness pass only.
			// Physical row indices survive filtering/reuse; no saved state, extra
			// VM call, readiness retry or gameplay operation is introduced here.
			if (!Logging::Enabled() || _state.preparing || _state.cancelled ||
				_state.passes + 1 < ImportJobs::kMaximumPasses) { return; }
			try {
				const auto* live = ref && !ref->IsDeleted() ? ref : nullptr;
				const auto* base = live ? live->data.objectReference : nullptr;
				const auto* file = base ? base->GetFile(0) : nullptr;
				const auto baseID = base ? base->formID : 0;
				const auto localID = baseID & ((baseID >> 24) == 0xFE ? 0xFFFu : 0xFFFFFFu);
				const auto* editorID = base ? base->GetFormEditorID() : nullptr;
				const RE::NiPoint3 position{ live ? live->data.location.x : 0.0F,
					live ? live->data.location.y : 0.0F, live ? live->data.location.z : 0.0F };
				const auto* loaded = live ? live->loadedData : nullptr;
				const auto& row = _state.rows[index];
				F4SE::log::info("Import preparation stack {}, physical row {} (zero-based) pending: {}; ref {:08X}, base {:08X}, plugin {}, local {:06X} (decimal {}), editor ID {}, type {}, cell {:08X}, refFlags {:08X}, has3D {}, loadedData {}, loadedFlags {:08X}, havokEligible {}, handled {}, position [{}, {}, {}]",
					_stackID, index, reason, ref ? ref->formID : 0, baseID,
					file ? file->GetFilename() : std::string_view{ "<unavailable>" }, localID, localID,
					editorID ? editorID : "", base ? static_cast<std::uint32_t>(base->GetFormType()) : 0,
					live && live->parentCell ? live->parentCell->formID : 0,
					live ? live->GetFormFlags() : 0u, live && live->Get3D(), loaded != nullptr,
					loaded ? loaded->flags : 0u, row.eligible, row.handled, position.x, position.y, position.z);
			} catch (...) {
				// Failure-only diagnostics must never change import completion.
			}
		}
	}

	bool AcquireLease()
	{
		const auto handle = ImportToolHandle(_tool);
		if (!handle) { return false; }
		std::scoped_lock lock(g_importLock);
		auto& active = g_importLeases[handle];
		if (active.lock()) { return false; }
		_lease = std::make_shared<ImportLease>();
		active = _lease;
		return true;
	}

	void ProcessRow(RE::TESObjectREFR* tool, RE::TESObjectREFR* ref, std::size_t index, ImportJobs::Row& row)
	{
		using ImportJobs::RowStage;
		if constexpr (Operation == ImportOperation::Prepare) {
			if (row.stage == RowStage::Initial) {
				row.eligible = _disableHavok && !ref->As<RE::Actor>();
				row.transform = { ref->data.location.x, ref->data.location.y, ref->data.location.z,
					ref->data.angle.x, ref->data.angle.y, ref->data.angle.z };
				for (const auto value : row.transform) {
					if (!std::isfinite(value)) { row.stage = RowStage::Failed; return; }
				}
				EngineAPI::Enable(ref, false);
				row.stage = RowStage::Waiting;
				LogPreparationPending(ref, index, "enabled-awaiting-readiness");
				return;
			}
			if (!ref->Get3D()) { LogPreparationPending(ref, index, "3D-not-loaded"); return; }
			// Binding can finish during enable/load. Recheck here, immediately
			// before motion changes, and preserve Havok for confirmed motors.
			// The existing row flag records this exception without a new payload.
			if (row.eligible) {
				const auto motor = ImportAnimation::HasGenericMotor(ref);
				if (!motor) { LogPreparationPending(ref, index, "motor-script-inspection-unavailable"); return; }
				if (*motor) {
					row.eligible = false;
				}
			}
			if (row.eligible) {
				const auto vm = GetVirtualMachine();
				if (!vm) { LogPreparationPending(ref, index, "VM-unavailable"); return; }
				if (!EngineAPI::ApplyKeyframedMotion(vm.get(), _stackID, ref, true)) { LogPreparationPending(ref, index, "keyframed-motion-not-ready"); return; }
				row.handled = true;
				const auto& t = row.transform;
				if (ref->data.location.x != t[0] || ref->data.location.y != t[1] || ref->data.location.z != t[2] ||
					ref->data.angle.x != t[3] || ref->data.angle.y != t[4] || ref->data.angle.z != t[5]) {
					if (!ref->parentCell) { row.stage = RowStage::Failed; return; }
					RE::NiPoint3 position{ t[0], t[1], t[2] };
					RE::NiPoint3 angle{ t[3], t[4], t[5] };
					auto invalidHandle = EngineAPI::InvalidRefHandle();
					EngineAPI::MoveRefrToPosition(ref, &invalidHandle, ref->parentCell, ref->GetWorldSpace(), &position, &angle);
					row.restored = true;
				}
			}
			row.stage = RowStage::Complete;
		} else if constexpr (Operation == ImportOperation::Generators) {
			auto* keyword = RE::TESForm::GetFormByID<RE::BGSKeyword>(0x4455B);
			auto* power = RE::TESForm::GetFormByID<RE::ActorValueInfo>(0x32E);
			if (!keyword || !power) { row.stage = RowStage::Failed; return; }
			row.generator = ref->Get3D() && ref->HasKeyword(keyword, nullptr) && ref->GetActorValue(*power) > 0.0F;
			row.stage = RowStage::Complete;
		} else {
			auto* defaultObject = RE::TESForm::GetFormByEditorID<RE::BGSDefaultObject>(RE::BSFixedString{ "WorkshopItem" });
			auto* keyword = defaultObject ? defaultObject->GetForm<RE::BGSKeyword>() : nullptr;
			auto* workshop = keyword && tool ? EngineAPI::GetLinkedRef(tool, keyword) : nullptr;
			if (!keyword || !workshop) { row.stage = RowStage::Failed; return; }
			// Co-save VM handles are remapped. Preserve counted-candidate flags,
			// while resuming partial point scans from zero against the live graph.
			if (_cursors[index].started && _cursors[index].sourceFormID == 0) { _cursors[index].sourceFormID = ref->formID; }
			const auto progress = ConduitConnections::ProcessReference(ref, keyword, workshop, _connections, _cursors[index], 16, PowerMode);
			if (progress == ConduitConnections::Progress::Complete) { row.stage = RowStage::Complete; }
			else { row.stage = RowStage::Waiting; }
			_pointWorkRemaining = progress == ConduitConnections::Progress::InProgress;
		}
	}

	void Finish(RE::BSScript::Variable& result)
	{
		_tracePending = false;
		_importNotice.Stop();
		using ImportJobs::RowStage;
		std::int32_t placed{}, ready{}, eligible{}, handled{}, restored{}, pending{}, failed{};
		for (const auto& row : _state.rows) {
			placed += row.stage != RowStage::Missing;
			ready += row.stage == RowStage::Complete;
			eligible += row.eligible;
			handled += row.handled;
			restored += row.restored;
			pending += row.stage == RowStage::Initial || row.stage == RowStage::Waiting;
			failed += row.stage == RowStage::Failed;
		}
		const auto tailMs = _lastRun == std::chrono::steady_clock::time_point{} ? 0 :
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _lastRun).count();
		const auto milliseconds = static_cast<std::int32_t>(std::min<std::int64_t>(static_cast<std::int64_t>(_state.elapsedMs) + tailMs, INT32_MAX));
		std::int32_t status = _state.cancelled ? -1 : (pending || failed ? 0 : 1);
		if constexpr (RowResults && Operation == ImportOperation::Reconnect && PowerMode == ConduitConnections::ExecutionMode::ConnectionsOnly) {
			ConduitConnections::SealAssemblyPlan(_connections.assemblyPlan, status == 1 && !_connections.counts.HasFailures());
		}
		if constexpr (Operation == ImportOperation::Generators) {
			ReferenceArray generators;
			const auto array = RE::BSScript::get<RE::BSScript::Array>(_references);
			if (array && !_state.cancelled) {
				for (std::size_t index = 0; index < _state.rows.size(); ++index) {
					if (_state.rows[index].generator) {
						if (auto* ref = UnpackSafely<RE::TESObjectREFR*>(array->elements[static_cast<std::uint32_t>(index)]).value_or(nullptr)) { generators.push_back(ref); }
						else { ++failed; }
					}
				}
			}
			if (_state.cancelled || failed || pending) { result = nullptr; }
			else {
				// A successful zero-length native array compares equal to None in
				// the observed Papyrus caller. A single null row explicitly means
				// "no generators to initialize"; None remains a genuine failure.
				if (generators.empty()) { generators.push_back(nullptr); }
				RE::BSScript::PackVariable(result, std::move(generators));
			}
		} else {
			ImportSummary summary;
			if constexpr (Operation == ImportOperation::Prepare) {
				for (const auto value : { status, placed, ready, eligible, handled, restored, pending, failed, milliseconds }) { summary.push_back(value); }
				if constexpr (RowResults) {
					summary.push_back(_state.done && !_state.cancelled && _cleanupErrors == 0 ? 1 : 0);
					for (std::size_t i = 0; i < _state.rows.size(); ++i) {
						const auto outcome = ImportRows::Classify(_state.rows[i]);
						summary.push_back(outcome);
						if (outcome == ImportRows::Failed || outcome == ImportRows::Pending) {
							const auto array = RE::BSScript::get<RE::BSScript::Array>(_references);
							auto* ref = array && i < array->size() ? UnpackSafely<RE::TESObjectREFR*>(array->elements[static_cast<std::uint32_t>(i)]).value_or(nullptr) : nullptr;
							LogExcludedImportRow("preparation", i, ref, outcome);
						}
					}
				}
			} else {
				const auto& c = _connections.counts;
				if (c.HasFailures() && status == 1) { status = 0; }
				const auto narrow = [](std::uint64_t value) { return static_cast<std::int32_t>(std::min<std::uint64_t>(value, INT32_MAX)); };
				for (const auto value : { status, narrow(c.candidates), narrow(c.points), narrow(c.matches), narrow(c.added), narrow(c.existing), pending,
					narrow(static_cast<std::uint64_t>(failed) + c.failed), milliseconds }) { summary.push_back(value); }
			}
			RE::BSScript::PackVariable(result, std::move(summary));
		}
		if (status != 1 || failed || pending) {
			F4SE::log::warn("Import stage {} stack {} incomplete: status {}, failed {}, pending {}", Name, _stackID, status, failed, pending);
		}
		if constexpr (PowerMode != ConduitConnections::ExecutionMode::Combined) {
			ConduitConnections::LogAssemblyPlan(_connections, PowerMode == ConduitConnections::ExecutionMode::ConnectionsOnly ? "assembly" : "refresh");
			if (!_connections.trace) {
				F4SE::log::warn("Conduit pair trace unavailable for {} stack {}; no cross-pass membership conclusion is possible", Name, _stackID);
			}
			ConduitConnections::LogPairTrace(_connections, PowerMode == ConduitConnections::ExecutionMode::ConnectionsOnly, _state.cancelled);
			// The original pass summary and completion status remain unchanged.
			CLIPBOARD_DEBUG_LOG(F4SE::log::info("Import power diagnostic {} stack {}: status {}, elapsedMs {}, {}",
				Name, _stackID, status, milliseconds, ConduitConnections::Describe(_connections.counts)));
			if (PowerMode == ConduitConnections::ExecutionMode::RefreshOnly || status != 1) {
				if (_powerProgress) { _powerProgress->ReleaseAssemblyMembership(); }
				_connections.assemblyPlan.reset();
			}
		}
		_lease.reset();
	}

	RE::BSScript::Variable _tool;
	RE::BSScript::Variable _references;
	RE::BSScript::Variable _originalReferences; // Saved only by the new scoped power factory.
	ImportJobs::State _state;
	ConduitConnections::Batch _connections;
	std::vector<ConduitConnections::ReferenceCursor> _cursors;
	std::vector<std::size_t> _powerDirtyRows; // Transient published checkpoint delta.
	std::shared_ptr<ImportLease> _lease;
	std::shared_ptr<PowerProgressSession> _powerProgress;
	std::chrono::steady_clock::time_point _lastRun;
	std::int32_t _delayMs{ 1 };
	bool _disableHavok{};
	bool _started{};
	bool _pointWorkRemaining{};
	bool _traceAttached{}, _tracePreDone{}, _tracePending{}; // Transient, never saved.
	std::size_t _tracePreCursor{}, _traceEndCursor{};
	bool _planAttached{}, _planPreDone{}; // Transient; unfinished scoped jobs cancel after load.
	std::size_t _planPreCursor{}, _planEndCursor{};
	std::uint32_t _cleanupErrors{};
	ImportProgress::Notice _importNotice;
};

using PrepareImportedObjectsFunctor = ImportFunctor<ImportOperation::Prepare, kPrepareImportedObjectsName>;
using GetImportedPowerGeneratorsFunctor = ImportFunctor<ImportOperation::Generators, kGetImportedPowerGeneratorsName>;
using ReconnectImportedPowerFunctor = EnginePowerFunctor<ImportFunctor<ImportOperation::Reconnect, kReconnectImportedPowerName>>;
using PrepareImportedRowsFunctor = ImportFunctor<ImportOperation::Prepare, kPrepareImportedRowsName, true>;
using ReconnectImportedPowerForRowsFunctor = EnginePowerFunctor<ImportFunctor<ImportOperation::Reconnect, kReconnectImportedPowerForRowsName, true>>;
using ConnectImportedPowerForRowsFunctor = EnginePowerFunctor<ImportFunctor<ImportOperation::Reconnect, kConnectImportedPowerForRowsName, true,
	ConduitConnections::ExecutionMode::ConnectionsOnly>>;
using RefreshImportedPowerForRowsFunctor = EnginePowerFunctor<ImportFunctor<ImportOperation::Reconnect, kRefreshImportedPowerForRowsName, true,
	ConduitConnections::ExecutionMode::RefreshOnly>>;
