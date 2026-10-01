// SPDX-License-Identifier: GPL-3.0-or-later
// Included after the pattern and existing-endpoint helpers in Clipboard.cpp.
namespace
{
	using PowerCancellation = Clipboard::Latent::PowerCancellation;
	using PowerWorkJob = Clipboard::Latent::PowerWorkJob;

	std::vector<ObjectRefHandle> PowerRowHandles(const VMArray<TESObjectREFR*>& rows)
	{
		std::vector<ObjectRefHandle> handles;
		handles.reserve(rows.size());
		for (auto* row : rows) { handles.push_back(row ? row->GetHandle() : ObjectRefHandle{}); }
		return handles;
	}

	class PatternWireJob final : public PowerWorkJob
	{
	public:
		PatternWireJob(UInt32 stack, TESObjectREFR* tool, UInt32 slot,
			VMArray<TESObjectREFR*> rows, bool explicitRows, PowerCancellation cancelled) :
			_stack(stack), _slot(slot), _tool(tool ? tool->GetHandle() : ObjectRefHandle{}),
			_cancelled(std::move(cancelled))
		{
			ScopedImportLog traceScope;
			if (!tool || tool->IsDeleted() || IsCancelled()) { _done = true; return; }
			auto* workshop = workshopItemKeyword ? GetLinkedRef_Native(tool, workshopItemKeyword) : nullptr;
			_workshop = workshop ? workshop->GetHandle() : ObjectRefHandle{};
			if (!explicitRows) { rows = GetSelectedObjectReferences(nullptr, tool); }
			const auto patternObjects = GetPatternObjects(nullptr, slot);
			if (rows.Length() != patternObjects.Length()) {
				CLIPBOARD_DEBUG_LOG(AppendToLog(
					"WARNING: placed result row count does not match the pattern; wire paste aborted."));
				_done = true;
				return;
			}
			_rows = PowerRowHandles(rows);
			_wireEntries = GetPatternWires(nullptr, slot);
			std::ifstream file(GetPatternFilePath(slot));
			_external = ExistingPower::Read(file);
			if (_external.present && !_external.valid) {
				logger::warn("External power import slot {} skipped: {}", slot, _external.error);
			} else if (_external.present) {
				_plugins = GetPatternPlugins(nullptr, slot);
				_origin = ReadExistingPowerOrigin(slot, _plugins);
				_externalAtOrigin = AtExistingPowerOrigin(tool, _origin);
				if (!_externalAtOrigin) {
					CLIPBOARD_DEBUG_LOG(logger::info("External power import slot {} ignored: Clipboard is not at the recorded source position", slot));
				}
			}
		}

		bool RunSlice(VMArray<TESObjectREFR*>& result) override
		{
			ScopedImportLog traceScope;
			if (IsCancelled()) { Cancel(); return true; }
			if (_done) { Finish(result); return true; }
			const auto tool = _tool.get();
			const auto workshop = _workshop.get();
			if (!tool || tool->IsDeleted() || !workshop || workshop->IsDeleted() ||
				!workshopItemKeyword || GetLinkedRef_Native(tool.get(), workshopItemKeyword) != workshop.get()) {
				Cancel();
				return true;
			}
			// A single wire is an indivisible engine transaction. Cancellation is
			// checked before starting it, never after placing half of a spline.
			if (_nextInternal < _wireEntries.Length()) {
				ProcessInternal(workshop.get(), _nextInternal++);
				return false;
			}
			if (_external.present && _external.valid && _externalAtOrigin && _nextExternal < _external.wires.size()) {
				ProcessExternal(tool.get(), workshop.get(), _nextExternal++);
				return false;
			}
			if (_external.present && _external.valid && _externalAtOrigin) {
				CLIPBOARD_DEBUG_LOG(logger::info("External power import slot {}: endpoints {}, requested {}, created {}, skipped {}",
					_slot, _external.endpoints.size(), _external.wires.size(), _createdExternal, _skippedExternal));
			}
			_done = true;
			Finish(result);
			return true;
		}

		void Cancel() noexcept override
		{
			_done = true;
			_newWires.clear();
			_rows.clear();
			_wireEntries = {};
			_plugins = {};
			_tool.reset();
			_workshop.reset();
		}

	private:
		bool IsCancelled() const { return _cancelled && _cancelled(); }

		NiPointer<TESObjectREFR> Row(UInt32 row, TESObjectREFR* workshop)
		{
			if (row >= _rows.size()) { return {}; }
			auto reference = _rows[row].get();
			auto* owner = reference && workshopItemKeyword ? GetLinkedRef_Native(reference.get(), workshopItemKeyword) : nullptr;
			if (!reference || reference->IsDeleted() || (owner && owner != workshop) ||
				!reference->data.objectReference || reference->data.objectReference->formID == BOTTLECAP_FORM_ID) {
				return {};
			}
			return reference;
		}

		void RecordWire(TESObjectREFR* reference)
		{
			if (reference) { _newWires.push_back(reference->GetHandle()); }
		}

		void ProcessInternal(TESObjectREFR* workshop, UInt32 row)
		{
			PatternWireEntry wire;
			UInt32 first{}, second{};
			if (!_wireEntries.Get(&wire, row) || !wire.Get<UInt32>("attachmentIndex1", &first) ||
				!wire.Get<UInt32>("attachmentIndex2", &second)) {
				CLIPBOARD_DEBUG_LOG(AppendToLog("WARNING: wire row " + std::to_string(row) + " has malformed attachment indices."));
				return;
			}
			const auto source = Row(first, workshop);
			const auto target = Row(second, workshop);
			if (!source || !target) {
				CLIPBOARD_DEBUG_LOG(AppendToLog("WARNING: wire row " + std::to_string(row) +
					" skipped because a physical endpoint row is missing, deleted, filtered or belongs to another workshop."));
				return;
			}
			if (!IsCancelled()) { RecordWire(AttachWireLatent(_stack, source.get(), target.get(), nullptr)); }
		}

		void ProcessExternal(TESObjectREFR* tool, TESObjectREFR* workshop, std::size_t row)
		{
			// Recheck before each engine transaction in case the tool moved while
			// the latent job was queued or while internal wires were being created.
			if (!AtExistingPowerOrigin(tool, _origin)) { _externalAtOrigin = false; return; }
			const auto reject = [&](const char* reason) {
				++_skippedExternal;
				logger::warn("External wire import slot {} row {} skipped: {}", _slot, row, reason);
			};
			if (!_external.wires[row]) { reject("malformed external wire record"); return; }
			const auto wire = *_external.wires[row];
			if (wire.objectRow >= _rows.size() || wire.endpointRow >= _external.endpoints.size() ||
				!_external.endpoints[wire.endpointRow]) {
				reject("invalid object or existing-endpoint row"); return;
			}
			const auto& recorded = *_external.endpoints[wire.endpointRow];
			auto* form = recorded.created ? LookupFormByID(recorded.reference.local) : ResolveExistingForm(recorded.reference, _plugins);
			const NiPointer<TESObjectREFR> endpoint{ form ? form->As<TESObjectREFR>() : nullptr };
			const auto source = Row(wire.objectRow, workshop);
			const ExistingPower::Eligibility eligibility{
				(recorded.created ? IsCreatedExistingReference(endpoint.get()) : IsStableExistingReference(endpoint.get())) &&
					(_external.version == 1 || (endpoint && ExistingPower::SamePosition(recorded.position,
						{endpoint->data.location.x, endpoint->data.location.y, endpoint->data.location.z}))),
				endpoint && !(endpoint->GetFormFlags() & ((1u << 5) | (1u << 11))),
				endpoint && endpoint->data.objectReference == ResolveExistingForm(recorded.base, _plugins),
				endpoint && endpoint->parentCell == ResolveExistingForm(recorded.cell, _plugins),
				workshop == ResolveExistingForm(recorded.workshop, _plugins),
				ExistingEndpointInside(endpoint.get(), workshop), ExistingEndpointOwnerAllowed(endpoint.get(), workshop),
				ExistingEndpointPolicy(endpoint.get()), endpoint && endpoint->Get3D() };
			if (const auto* reason = ExistingPower::Reject(eligibility)) {
				logger::warn("External endpoint slot {} row {} identity {}|{} expected base {}|{}: {}",
					_slot, wire.endpointRow, recorded.reference.plugin, recorded.reference.local,
					recorded.base.plugin, recorded.base.local, reason);
				reject(reason); return;
			}
			if (tool->IsDeleted() || workshop->IsDeleted() || !source ||
				(source->GetFormFlags() & (1u << 11)) || !source->Get3D() ||
				GetLinkedRef_Native(source.get(), workshopItemKeyword) != workshop ||
				!ExistingEndpointInside(source.get(), workshop)) {
				reject("imported endpoint unavailable or outside its workshop"); return;
			}
			if (source == endpoint || !_visited.emplace(source->formID, endpoint->formID).second) {
				reject("self-connection or duplicate external wire"); return;
			}
			if (IsCancelled()) { return; }
			// The selected endpoint owns the new wire. Do not mutate the existing
			// endpoint's ownership, selection, transform or enabled state.
			if (auto* placed = AttachWireLatent(_stack, source.get(), endpoint.get(), nullptr)) {
				RecordWire(placed);
				++_createdExternal;
			} else { reject("engine attachment rejected (including an already existing wire)"); }
		}

		void Finish(VMArray<TESObjectREFR*>& result)
		{
			for (const auto& handle : _newWires) {
				const auto wire = handle.get();
				if (wire && !wire->IsDeleted()) { result.push_back(wire.get()); }
			}
			_newWires.clear();
		}

		UInt32 _stack{}, _slot{};
		ObjectRefHandle _tool, _workshop;
		PowerCancellation _cancelled;
		std::vector<ObjectRefHandle> _rows, _newWires;
		VMArray<PatternWireEntry> _wireEntries;
		VMArray<BSFixedString> _plugins;
		ExistingPower::Bundle _external;
		std::optional<ExistingPowerOrigin> _origin;
		std::set<std::pair<UInt32, UInt32>> _visited;
		UInt32 _nextInternal{};
		std::size_t _nextExternal{}, _createdExternal{}, _skippedExternal{};
		bool _done{}, _externalAtOrigin{};
	};

	class TransmitSelectionJob final : public PowerWorkJob
	{
	public:
		TransmitSelectionJob(TESObjectREFR* tool, PowerCancellation cancelled) :
			_tool(tool ? tool->GetHandle() : ObjectRefHandle{}), _cancelled(std::move(cancelled))
		{
			if (tool && !tool->IsDeleted() && !IsCancelled()) {
				_rows = PowerRowHandles(GetSelectedObjectReferences(nullptr, tool));
			}
			_batch.cancelled = _cancelled;
		}

		bool RunSlice(VMArray<TESObjectREFR*>&) override
		{
			if (IsCancelled()) { Cancel(); return true; }
			const auto tool = _tool.get();
			if (!tool || tool->IsDeleted() || _next == _rows.size()) { return true; }
			const auto source = _rows[_next].get();
			using namespace Clipboard::ConduitConnections;
			if (!source || source->IsDeleted() || ProcessReference(source.get(),
				GetDefaultForm<BGSKeyword>("WorkshopItem"), nullptr, _batch, _cursor, 16) != Progress::InProgress) {
				++_next;
				_cursor = {};
			}
			return _next == _rows.size();
		}

		void Cancel() noexcept override
		{
			_rows.clear();
			_next = 0;
			_tool.reset();
		}

	private:
		bool IsCancelled() const { return _cancelled && _cancelled(); }
		ObjectRefHandle _tool;
		PowerCancellation _cancelled;
		std::vector<ObjectRefHandle> _rows;
		std::size_t _next{};
		Clipboard::ConduitConnections::Batch _batch;
		Clipboard::ConduitConnections::ReferenceCursor _cursor;
	};

	std::shared_ptr<PowerWorkJob> CreatePatternWireJob(UInt32 stack, TESObjectREFR* tool, UInt32 slot,
		VMArray<TESObjectREFR*> rows, bool explicitRows, PowerCancellation cancelled)
	{
		return std::make_shared<PatternWireJob>(stack, tool, slot, std::move(rows), explicitRows, std::move(cancelled));
	}

	std::shared_ptr<PowerWorkJob> CreateTransmitPowerJob(UInt32, TESObjectREFR* tool, PowerCancellation cancelled)
	{
		return std::make_shared<TransmitSelectionJob>(tool, std::move(cancelled));
	}
}
