// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
// Included inside Clipboard::Latent's private namespace after ImportFunctors.inl.


class WorkshopReferenceCompletion final : public RE::BSScript::IStackCallbackFunctor
{
public:
	WorkshopReferenceCompletion(std::shared_ptr<WorkshopCallbacks::CompletionSignal> signal, std::shared_ptr<ImportLease> lease,
		std::shared_ptr<ImportProgress::RegistrationProgress> progress, std::uint32_t row, std::uint8_t progressBit) :
		_signal(std::move(signal)), _lease(std::move(lease)), _progress(std::move(progress)), _row(row), _progressBit(progressBit) {}
	~WorkshopReferenceCompletion() override
	{
		// Dropping a callback without a terminal notification cannot certify its
		// callee has stopped. Preserve a conservative session block in that case.
		if (_signal->Get() == WorkshopCallbacks::Completion::Pending) { g_cleanupGate.MarkUncertain(); }
	}

	void CallQueued() override {}
	void CallCanceled() override { Resolve(WorkshopCallbacks::Completion::Cancelled); }
	// The reviewed bound-object DispatchMethodCall overload does not enter
	// multi-dispatch. These hooks are deliberately not completion notifications.
	void StartMultiDispatch() override {}
	void EndMultiDispatch() override {}
	void operator()(RE::BSScript::Variable) override { Resolve(WorkshopCallbacks::Completion::Returned); }
	bool CanSave() override { return false; }

private:
	void Resolve(WorkshopCallbacks::Completion outcome) noexcept
	{
		// A timed-out/cancelled job can release its own lease. Keep the tool busy
		// until each real callee returns/cancels, even after the job is destroyed.
		// Release before publishing completion so a finished batch has no stale
		// callback-owned lease. Duplicate notifications are safe.
		std::scoped_lock lock(_completionLock);
		const bool first = _signal->Get() == WorkshopCallbacks::Completion::Pending;
		_lease.reset();
		_workTicket.Complete();
		if (first && outcome == WorkshopCallbacks::Completion::Returned && _progress && _progressBit) {
			_progress->Returned(_row, _progressBit);
		}
		_signal->Resolve(outcome);
	}
	std::shared_ptr<WorkshopCallbacks::CompletionSignal> _signal;
	std::mutex _completionLock;
	std::shared_ptr<ImportLease> _lease;
	std::shared_ptr<ImportProgress::RegistrationProgress> _progress;
	std::uint32_t _row{};
	std::uint8_t _progressBit{};
	LegacyCleanup::WorkTicket _workTicket{ g_cleanupGate };
};

template <bool RowResults = false>
class WorkshopInitializationFunctor final : public LatentFunctorBase
{
public:
	explicit WorkshopInitializationFunctor(F4SE::SerializationTag tag) : LatentFunctorBase(tag) {}
	explicit WorkshopInitializationFunctor(std::uint32_t stackID, RE::TESObjectREFR* tool, ReferenceArray rows) :
		LatentFunctorBase(stackID),
		_useThrottlingRequested(!g_callbacks.useWorkshopThrottling || g_callbacks.useWorkshopThrottling()),
		_singleCallbackRequested(_useThrottlingRequested && g_callbacks.useSingleWorkshopCallback && g_callbacks.useSingleWorkshopCallback()),
		_callbackLimitRequested(_useThrottlingRequested && g_callbacks.workshopCallbackLimit ? g_callbacks.workshopCallbackLimit() : -1),
		_callbackResumeRequested(_useThrottlingRequested && g_callbacks.workshopCallbackResumeAt ? g_callbacks.workshopCallbackResumeAt() : -1),
		_state(CheckedCount(rows.size()), WorkshopCallbacks::SchedulingPolicy::ContinuationsFirst,
			_useThrottlingRequested,
			WorkshopCallbacks::kDefaultOutstanding, _singleCallbackRequested, _callbackLimitRequested, _callbackResumeRequested),
		_spacing(_useThrottlingRequested && g_callbacks.workshopCallbackSpacingMs ? g_callbacks.workshopCallbackSpacingMs() : 0,
			_state.UsesThrottling(), _state.AdmissionLimit() == 1),
		_signals(_state.Capacity()), _dispatchInfo(_state.Capacity()),
		_progress(std::make_shared<ImportProgress::RegistrationProgress>(rows.size())), _progressOwner(tool ? tool->formID : 0)
	{
		// This pass only examines incoming native pointers: no per-row VM calls,
		// engine mutations, or callback dispatch occurs on the registering stack.
		std::uint32_t index{};
		for (auto* ref : rows) {
			if (!ref) { (void)_state.SkipRow(index); }
			++index;
		}
		RE::BSScript::PackVariable(_tool, tool);
		RE::BSScript::PackVariable(_references, std::move(rows));
	}
	~WorkshopInitializationFunctor() override
	{
		if (_progress) { _progress->Finish(false); }
	}

	[[nodiscard]] const char* ClassName() const override { return RowResults ? kInitializeImportedWorkshopRowsName : kInitializeImportedWorkshopObjectsName; }
	[[nodiscard]] std::uint32_t ClassVersion() const override { return WorkshopCallbacks::kCurrentVersion; }

	bool Save(const F4SE::SerializationInterface* intfc) override
	{
		try {
			if (_lease && _lease->cancelled.load()) { _state.Cancel(); }
			_state.elapsedMs = std::max(_state.elapsedMs, Elapsed());
			if (!SaveStack(intfc) || !WriteVariable(intfc, _tool) || !WriteVariable(intfc, _references) ||
				!WriteVariable(intfc, _workshop) || !_state.Save([&](const auto& value) { return WriteExact(intfc, value); })) { return false; }
			for (const auto& info : _dispatchInfo) {
				// These IDs are diagnostics only. They are never resolved or used to
				// replay a call after load; authoritative refs remain VM variables.
				if (!WriteExact(intfc, info.targetID) || !WriteExact(intfc, info.argumentID)) { return false; }
			}
			return true;
		} catch (...) { return false; }
	}

	bool Load(const F4SE::SerializationInterface* intfc, std::uint32_t version) override
	{
		struct ValidationGuard
		{
			bool accepted{};
			~ValidationGuard()
			{
				// A malformed/unsupported payload may accompany retained VM callee
				// stacks. Discarding its functor is not evidence that they finished.
				if (!accepted) { g_cleanupGate.MarkUncertain(); }
			}
		} validation;
		try {
			if ((version < 1 || version > ClassVersion()) || !LoadStack(intfc) || !ReadVariable(intfc, _tool) ||
				!ReadVariable(intfc, _references) || !ReadVariable(intfc, _workshop) ||
				!_state.Load([&](auto& value) { return ReadExact(intfc, value); }, version) || !_references.is<RE::BSScript::Array>()) { return false; }
			const auto array = RE::BSScript::get<RE::BSScript::Array>(_references);
			if (!array || array->size() != _state.rows.size()) { return false; }
			_signals.resize(_state.Capacity());
			_dispatchInfo.resize(_state.Capacity());
			for (auto& info : _dispatchInfo) {
				if (!ReadExact(intfc, info.targetID) || !ReadExact(intfc, info.argumentID)) { return false; }
			}
			_loaded = true;
			if (_state.outstanding) {
				// The callback itself is not serializable. Even after this restored
				// functor retires, its callee may still run without a return signal.
				g_cleanupGate.MarkUncertain();
				F4SE::log::warn("Legacy cleanup disabled for this session: restored workshop job has {} uncertain callbacks", _state.outstanding);
			}
			// State::Load marks every unfinished job interrupted/done. Never
			// recreate uncertain callbacks: their VM stacks may still be running.
			validation.accepted = true;
			return true;
		} catch (...) { return false; }
	}

	bool ShouldReschedule(std::int32_t& delayMilliseconds) override
	{
		// Off drains all ready work in RunSlice. A remaining job is waiting
		// for actual callbacks (or its first HUD task), so yield to the VM.
		// F4SE requeues zero-delay jobs inside the same shared Papyrus budget;
		// busy polling there can starve the very callbacks we need to return.
		delayMilliseconds = 1;
		return !_state.done;
	}
	bool ShouldResumeStack(std::uint32_t& stackID) override
	{
		stackID = _stackID;
		return _state.done;
	}

	bool Run(RE::BSScript::Variable& result) override
	{
		try {
			if (!_started) {
				_started = true;
				_start = std::chrono::steady_clock::now();
				if (!_state.done && !Begin()) { _state.Cancel(); }
			}
			if (_lease && _lease->cancelled.load()) { _state.Cancel(); }
			UpdateProgress();
			// A bounded, read-only pass establishes the registration denominator.
			// Dispatch still repeats the authoritative no-op safety check.
			if (!_state.done && !ClassifyProgressRows()) { return true; }
			if (!_state.done && !_progressStarted) { _progress->Start(_progressOwner); _progressStarted = true; }
			// Consume actual returns before checking timeout. The VM may have
			// finished calls while this delayed functor was waiting to run.
			CollectCompletions();
			if (!_state.done) {
				auto* tool = UnpackSafely<RE::TESObjectREFR*>(_tool).value_or(nullptr);
				auto* workshop = UnpackSafely<RE::TESObjectREFR*>(_workshop).value_or(nullptr);
				if (!tool || tool->IsDeleted() || !workshop || workshop->IsDeleted() || !_lease || _lease->cancelled.load()) {
					_state.Cancel();
				}
				_state.Tick(Elapsed());
				const auto vm = GetVirtualMachine();
				const auto array = _references.is<RE::BSScript::Array>() ? RE::BSScript::get<RE::BSScript::Array>(_references) : nullptr;
				if (!vm || !array || array->size() != _state.rows.size()) { _state.Cancel(); }
				WorkshopCallbacks::RunSlice<false>(_state, WorkshopCallbacks::ClockNs, [this] {
					// A long Off pass may outlive the watchdog interval. Observe
					// other slots' real returns before Reserve evaluates timeout.
					const auto now = Elapsed();
					if (_state.TimeoutCheckDue(now)) { CollectCompletions(); return Elapsed(); }
					return now;
				},
					[&](const WorkshopCallbacks::Token& token) {
						// Off can run longer than one frame: retain live cancellation
						// and reminder polling inside the unrestricted ready-work loop.
						if (_lease->cancelled.load() || tool->IsDeleted() || workshop->IsDeleted()) {
							Reject(token); // reserved here, never entered the VM
							_state.Cancel();
							return;
						}
						UpdateProgress();
						auto* ref = UnpackSafely<RE::TESObjectREFR*>(array->elements[token.row]).value_or(nullptr);
						Dispatch(vm.get(), workshop, ref, token);
						// Empty handlers may return synchronously. Poll this request
						// here; poll the entire set once per slice, not once per row.
						if (_state.UsesThrottling() && _state.Capacity() <= WorkshopCallbacks::kMaximumOutstanding) { CollectCompletions(); }
						else { CollectCompletion(token.slot); }
					}, [&](std::uint32_t row, WorkshopCallbacks::Phase phase, std::uint64_t nowMs) {
						if (_spacing.effectiveMs && WorkshopCallbacks::IsWorkshopPhase(phase) && nowMs < _spacing.deadlineMs) {
							// Do not repeatedly walk VM bindings at every cooldown poll.
							// Cache only a decision to WAIT for this row/deadline. Dispatch
							// always repeats the authoritative check before an omission.
							if (_spacingCheckRow != row || _spacingCheckDeadline != _spacing.deadlineMs) {
								auto* ref = UnpackSafely<RE::TESObjectREFR*>(array->elements[row]).value_or(nullptr);
								if (CanOmitWorkshopPhase(vm.get(), workshop, ref)) { return true; }
								_spacingCheckRow = row;
								_spacingCheckDeadline = _spacing.deadlineMs;
							}
						}
						return _spacing.Allows(phase, nowMs);
					});
			}
			UpdateProgress();
			ObservePendingWaits();
			if (_state.done) { Finish(result); }
			return true;
		} catch (const std::exception& error) {
			try { F4SE::log::error("Workshop initialization stack {} failed: {}", _stackID, error.what()); } catch (...) {}
		} catch (...) {
			try { F4SE::log::error("Workshop initialization stack {} failed with an unknown exception", _stackID); } catch (...) {}
		}
		_state.Cancel();
		try { Finish(result); } catch (...) { result = nullptr; _lease.reset(); }
		return true;
	}

private:
	struct DispatchInfo
	{
		std::uint32_t targetID{}, argumentID{};
	};

	void UpdateProgress() noexcept
	{
		if (!_state.done && _progress) { _progress->Refresh(); }
	}

	bool ClassifyProgressRows()
	{
		if (_progressCursor == _state.rows.size()) { return true; }
		const auto vm = GetVirtualMachine();
		const auto array = _references.is<RE::BSScript::Array>() ? RE::BSScript::get<RE::BSScript::Array>(_references) : nullptr;
		auto* workshop = UnpackSafely<RE::TESObjectREFR*>(_workshop).value_or(nullptr);
		if (!vm || !array || !workshop || workshop->IsDeleted() || array->size() != _state.rows.size()) { _state.Cancel(); return true; }
		const auto start = WorkshopCallbacks::ClockNs();
		std::size_t visits{};
		while (_progressCursor < _state.rows.size()) {
			auto* ref = UnpackSafely<RE::TESObjectREFR*>(array->elements[static_cast<std::uint32_t>(_progressCursor)]).value_or(nullptr);
			if (ref && !CanOmitWorkshopPhase(vm.get(), workshop, ref)) { _progress->Require(_progressCursor, 3); }
			++_progressCursor;
			if (++visits >= 64 || WorkshopCallbacks::ClockNs() - start >= WorkshopCallbacks::kSliceBudgetNs) { break; }
		}
		return _progressCursor == _state.rows.size();
	}

	void Accept(const WorkshopCallbacks::Token& token)
	{
		if (_state.Accept(token)) { LogDrainTransition(); }
	}
	void Reject(const WorkshopCallbacks::Token& token)
	{
		(void)_state.Reject(token);
	}

	static std::uint32_t CheckedCount(std::size_t count)
	{
		if (count > WorkshopCallbacks::kMaximumRows) { throw std::length_error("Workshop callback row limit exceeded"); }
		return static_cast<std::uint32_t>(count);
	}

	[[nodiscard]] std::uint64_t Elapsed() const
	{
		if (_start == std::chrono::steady_clock::time_point{} || _loaded) { return _state.elapsedMs; }
		const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _start).count();
		return static_cast<std::uint64_t>(std::max<std::int64_t>(delta, 0));
	}

	bool Begin()
	{
		if (g_registrationState.load(std::memory_order_acquire) != RegistrationState::kReady) { return false; }
		auto* tool = UnpackSafely<RE::TESObjectREFR*>(_tool).value_or(nullptr);
		const auto handle = ImportToolHandle(_tool);
		if (!tool || tool->IsDeleted() || !handle) { return false; }
		{
			std::scoped_lock lock(g_importLock);
			auto& active = g_importLeases[handle];
			if (active.lock()) {
				F4SE::log::error("Workshop initialization stack {} rejected: tool already has an active or unresolved import job", _stackID);
				return false;
			}
			_lease = std::make_shared<ImportLease>();
			active = _lease;
		}
		auto* defaultObject = RE::TESForm::GetFormByEditorID<RE::BGSDefaultObject>(RE::BSFixedString{ "WorkshopItem" });
		auto* keyword = defaultObject ? defaultObject->GetForm<RE::BGSKeyword>() : nullptr;
		auto* workshop = keyword ? EngineAPI::GetLinkedRef(tool, keyword) : nullptr;
		if (!workshop || workshop->IsDeleted()) { return false; }
		RE::BSScript::PackVariable(_workshop, workshop);
		_resources = WorkshopNoOp::InspectResources();
		_waitSnapshotsEnabled = enableLogging && g_callbacks.enableWorkshopWaitSnapshots && g_callbacks.enableWorkshopWaitSnapshots();
		const bool earlyRequested = _waitSnapshotsEnabled && _state.UsesThrottling() && g_callbacks.enableWorkshopEarlyWaitSnapshot && g_callbacks.enableWorkshopEarlyWaitSnapshot();
		_waitSnapshots = WorkshopCallbacks::WaitSnapshotSchedule(WorkshopCallbacks::UseEarlyWaitSnapshot(
			earlyRequested, _waitSnapshotsEnabled, _state.UsesThrottling(), _singleCallbackRequested, _spacing.effectiveMs, _callbackLimitRequested >= 0));
		if ((_singleCallbackRequested || _callbackLimitRequested >= 0 || _spacing.requestedMs) && enableLogging) {
			CLIPBOARD_DEBUG_LOG(F4SE::log::info("Workshop callback spacing diagnostic: stack {}, requested {} ms, effective {} ms, admission limit {}, throttling {}; minimum from observed successful workshop placed return, object phases overlap gap; no receiver-idle guarantee",
				_stackID, _spacing.requestedMs, _spacing.effectiveMs, _state.AdmissionLimit(), _state.UsesThrottling()));
		}
		if (_singleCallbackRequested && enableLogging) {
			CLIPBOARD_DEBUG_LOG(F4SE::log::info("Workshop callback admission diagnostic: stack {}, requested single true, effective single {}, throttling {}, admission limit {}, storage slots {}; policy frozen for this job, direct callbacks only",
				_stackID, _state.UsesThrottling() && _state.AdmissionLimit() == 1, _state.UsesThrottling(), _state.AdmissionLimit(), _state.Capacity()));
		}
		if (_callbackLimitRequested >= 0 && enableLogging) {
			CLIPBOARD_DEBUG_LOG(F4SE::log::info("Workshop callback limit diagnostic: stack {}, requested {}, effective {}, rows {}, throttling {}, legacy single requested {}; explicit limit overrides single with On, Off ignores limit; watchdog {} {}ms, native budget ns {}",
				_stackID, _callbackLimitRequested, _state.AdmissionLimit(), _state.rows.size(), _state.UsesThrottling(), _singleCallbackRequested,
				_state.UsesThrottling() ? "per-request" : "no-return", WorkshopCallbacks::kCallTimeoutMs,
				_state.UsesThrottling() ? WorkshopCallbacks::kSliceBudgetNs : 0ULL));
		}
		if (_callbackResumeRequested >= 0 && enableLogging) {
			CLIPBOARD_DEBUG_LOG(F4SE::log::info("Workshop callback drain diagnostic: stack {}, requested resume at {}, effective {}, high {}, resume at {}; requires Throttling On, explicit finite limit and 0 <= resume < effective high; closed admission retains per-request timeout and final barrier",
				_stackID, _callbackResumeRequested, _state.drain.Enabled(), _state.drain.High(), _state.drain.ResumeAt()));
		}
		if (_waitSnapshotsEnabled && enableLogging) {
			CLIPBOARD_DEBUG_LOG(F4SE::log::info("Workshop wait snapshots enabled: stack {}, thresholds {} ms of oldest accepted pending request, at most {} attempts per job; final timeout dump independent; throttling {}; early requested {}, effective {}",
				_stackID, _waitSnapshots.EarlyEnabled() ? "5000/30000/60000/90000" : "30000/60000/90000",
				_waitSnapshots.EarlyEnabled() ? 4 : 3, _state.UsesThrottling(), earlyRequested, _waitSnapshots.EarlyEnabled()));
		}
		return _workshop.is<RE::BSScript::Object>();
	}

	static const char* PhaseName(WorkshopCallbacks::Phase phase) noexcept
	{
		switch (phase) {
		case WorkshopCallbacks::Phase::WorkshopPlaced: return "workshop.OnWorkshopObjectPlaced";
		case WorkshopCallbacks::Phase::ObjectPlaced: return "object.OnWorkshopObjectPlaced";
		case WorkshopCallbacks::Phase::WorkshopMoved: return "workshop.OnWorkshopObjectMoved";
		case WorkshopCallbacks::Phase::ObjectMoved: return "object.OnWorkshopObjectMoved";
		default: return "complete";
		}
	}

	bool CanOmitWorkshopPhase(RE::BSScript::IVirtualMachine* vm, RE::TESObjectREFR* workshop, RE::TESObjectREFR* ref)
	{
		if (!ref || ref->IsDeleted() || !_resources.Compatible()) { return false; }
		const auto kind = WorkshopNoOp::Classify(*vm, ref);
		return kind == WorkshopNoOp::Kind::Plain &&
			WorkshopNoOp::CanOmit(kind, WorkshopNoOp::CompatibleReceiver(*vm, workshop, _resources));
	}

	void Dispatch(RE::BSScript::IVirtualMachine* vm, RE::TESObjectREFR* workshop, RE::TESObjectREFR* ref, const WorkshopCallbacks::Token& token)
	{
		using WorkshopCallbacks::Phase;
		const bool onWorkshop = token.phase == Phase::WorkshopPlaced || token.phase == Phase::WorkshopMoved;
		const std::uint8_t progressBit = token.phase == Phase::WorkshopPlaced ? 1 : token.phase == Phase::WorkshopMoved ? 2 : 0;
		auto* target = onWorkshop ? workshop : ref;
		auto* argument = onWorkshop ? ref : workshop;
		// Keep the existing serialized fields/slot layout, but do not collect
		// diagnostic-only identities while their output is disabled.
		_dispatchInfo[token.slot] = enableLogging ? DispatchInfo{ target ? target->formID : 0, argument ? argument->formID : 0 } : DispatchInfo{};
		bool bridgeAttempted{};
		try {
			if (!ref || ref->IsDeleted() || !target || !argument) {
				Reject(token);
				LogRequest(token, "unavailable reference");
				return;
			}
			if (onWorkshop && CanOmitWorkshopPhase(vm, workshop, ref) && _state.Omit(token)) {
				_progress->Omit(token.row, progressBit);
				return;
			}
			auto signal = std::make_shared<WorkshopCallbacks::CompletionSignal>();
			_signals[token.slot] = signal;
			auto* completion = new WorkshopReferenceCompletion(signal, _lease, _progress, token.row, progressBit);
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ completion };
			const RE::BSFixedString name{ token.phase == Phase::WorkshopPlaced || token.phase == Phase::ObjectPlaced ?
				"OnWorkshopObjectPlaced" : "OnWorkshopObjectMoved" };
			bridgeAttempted = true;
			const bool accepted = EngineAPI::DispatchReferenceCallback(vm, target, name, argument, callback);
			if (accepted) {
				if (onWorkshop) { _progress->Require(token.row, progressBit); }
				Accept(token);
			} else {
				completion->CallCanceled();
				// The engine failure tail can synchronously call CallCanceled. The
				// dispatch rejection owns accounting; do not count that signal twice.
				Reject(token);
				_signals[token.slot].reset();
				LogRequest(token, "dispatch rejected");
			}
		} catch (...) {
			if (bridgeAttempted) {
				// Only the argument-builder lambda catches its own failures. Once
				// the bridge is entered, an exception cannot prove the VM rejected
				// the call. Preserve uncertain outstanding work and stop the job;
				// any VM-owned callback still holds its signal and tool lease.
				Accept(token);
				_state.Cancel();
				LogRequest(token, "dispatch threw; acceptance and side effects uncertain");
			} else {
				Reject(token);
				_signals[token.slot].reset();
				LogRequest(token, "request construction failed before dispatch");
			}
			throw;
		}
	}

	void CollectCompletions()
	{
		if (_state.done) { return; }
		// Off's watchdog measures observed successful returns. Timestamp this
		// observation before consuming signals, then evaluate timeout afterward.
		_state.elapsedMs = std::max(_state.elapsedMs, Elapsed());
		for (std::uint32_t index = 0; index < _signals.size(); ++index) {
			CollectCompletion(index);
		}
	}

	void CollectCompletion(std::uint32_t index)
	{
		if (_state.done) { return; }
		const auto& signal = _signals[index];
		const auto& slot = _state.slots[index];
		if (!signal || !slot.active || !slot.accepted) { return; }
		const auto outcome = signal->Get();
		if (outcome == WorkshopCallbacks::Completion::Pending) { return; }
		const auto token = slot.token;
		const auto observedMs = Elapsed();
		_state.elapsedMs = std::max(_state.elapsedMs, observedMs);
		const bool wasCancelled = outcome == WorkshopCallbacks::Completion::Cancelled;
		if (_state.Complete(token, wasCancelled)) {
			LogDrainTransition();
			if (!wasCancelled) {
				if (_spacing.effectiveMs) { _spacing.Returned(token.phase, observedMs); }
				if (_singleCallbackRequested || _callbackLimitRequested >= 0) {
					if (enableLogging) { _latency[static_cast<std::size_t>(token.phase)].Record(token, slot.startedMs, observedMs); }
					else { _latencyComplete = false; }
				}
			}
		}
		if (wasCancelled) { LogRequest(token, "VM callback cancelled"); }
		_signals[index].reset();
	}

	void LogDrainTransition() noexcept
	{
		if (!enableLogging) { return; }
		try {
			const auto& drain = _state.drain;
			const auto sequence = drain.Closures() + drain.Reopenings();
			if (!drain.Enabled() || sequence == _drainTransitionSeen) { return; }
			_drainTransitionSeen = sequence;
			// Log only transitions, bounded per job. Summary counters remain exact.
			if (!enableLogging || _drainTransitionLines >= 16) { return; }
			++_drainTransitionLines;
			CLIPBOARD_DEBUG_LOG(F4SE::log::info("Workshop callback drain {}: stack {}, transition {}, high {}, resume at {}, outstanding {}, returned {}, elapsed {} ms, admission held {} ms; transition detail {}/16 (later details suppressed, totals retained)",
				drain.Closed() ? "closed" : "reopened", _stackID, sequence, drain.High(), drain.ResumeAt(),
				_state.outstanding, _state.returned, _state.elapsedMs, drain.HeldMs(_state.elapsedMs), _drainTransitionLines));
		} catch (...) { /* Observation must not cancel or otherwise change the job. */ }
	}

	DispatchInfo DiagnosticIDs(const WorkshopCallbacks::Token& token) const
	{
		auto info = _dispatchInfo[token.slot];
		if (info.targetID || info.argumentID) { return info; }
		// Logging may have been enabled after this request was dispatched.
		// Resolve IDs only for the requested diagnostic, never from a reused
		// slot's preceding request. VM variables retain the actual references.
		const auto array = _references.is<RE::BSScript::Array>() ? RE::BSScript::get<RE::BSScript::Array>(_references) : nullptr;
		auto* ref = array && token.row < array->size() ? UnpackSafely<RE::TESObjectREFR*>(array->elements[token.row]).value_or(nullptr) : nullptr;
		auto* workshop = UnpackSafely<RE::TESObjectREFR*>(_workshop).value_or(nullptr);
		const auto referenceID = ref ? ref->formID : 0;
		const auto workshopID = workshop ? workshop->formID : 0;
		return WorkshopCallbacks::IsWorkshopPhase(token.phase) ? DispatchInfo{ workshopID, referenceID } : DispatchInfo{ referenceID, workshopID };
	}

	void LogRequest(const WorkshopCallbacks::Token& token, const char* reason) const
	{
		const auto info = DiagnosticIDs(token);
		F4SE::log::warn("Workshop callback stack {}, row {}, phase {}, target {:08X}, argument {:08X}: {}",
			_stackID, token.row, PhaseName(token.phase), info.targetID, info.argumentID, reason);
	}

	void LogPendingRequests(const char* reason, std::uint32_t snapshot, std::uint64_t nowMs) const
	{
		// Read-only observation of existing accounting. Request age includes
		// VM admission/queue time; it is not time spent executing the handler.
		// Signals can resolve during a snapshot. Generations distinguish reused
		// slots; observing a return here never acknowledges it to the scheduler.
		constexpr std::uint32_t detailLimit = 32;
		std::uint32_t observed{}, reported{};
		for (std::uint32_t index = 0; index < _state.Capacity(); ++index) {
			const auto& slot = _state.slots[index];
			if (!slot.active || !slot.accepted) { continue; }
			++observed;
			if (reported == detailLimit) { continue; }
			const auto info = DiagnosticIDs(slot.token);
			const auto& signal = _signals[index];
			const auto completion = signal ? signal->Get() : WorkshopCallbacks::Completion::Pending;
			const char* completionName = !signal ? "unavailable" :
				completion == WorkshopCallbacks::Completion::Returned ? "returned-at-snapshot" :
				completion == WorkshopCallbacks::Completion::Cancelled ? "cancelled-at-snapshot" : "pending";
			const auto age = nowMs >= slot.startedMs ? nowMs - slot.startedMs : 0;
			F4SE::log::info("Workshop {} request: stack {}, slot {}, row {} (zero-based), phase {}, target {:08X}, argument {:08X}, request age {} ms, at least {} ms old {}, signal {}, generation {}, snapshot {}",
				reason, _stackID, index, slot.token.row, PhaseName(slot.token.phase), info.targetID, info.argumentID,
				age, WorkshopCallbacks::kCallTimeoutMs, age >= WorkshopCallbacks::kCallTimeoutMs, completionName, slot.token.generation, snapshot);
			++reported;
		}
		F4SE::log::info("Workshop {} requests: stack {}, observed {}, reported {}, omitted {}; ages include VM queue time; signals sampled non-atomically; snapshot {}",
			reason, _stackID, observed, reported, observed - reported, snapshot);
	}

	void DumpPapyrusStacks(const char* reason, std::uint32_t snapshot) const
	{
		// Same reviewed shared-prefix debug interface as the retained timeout
		// dump. No hook, new address, object mutation or retained VM pointer.
		static_assert(offsetof(RE::GameVM, debugInterface) == 0xC0);
		const auto gameVM = RE::GameVM::GetSingleton();
		if (gameVM && gameVM->debugInterface) {
			const auto start = Elapsed();
			F4SE::log::info("Workshop {} stack dump: stack {}, snapshot {}, request at {} ms; running Papyrus stacks requested in Papyrus.0.log",
				reason, _stackID, snapshot, start);
			gameVM->debugInterface->DumpRunningStacksToLog();
			const auto end = Elapsed();
			F4SE::log::info("Workshop {} stack dump request returned: stack {}, snapshot {}, elapsed {} ms, request duration {} ms; not a callback-completion barrier",
				reason, _stackID, snapshot, end, end - start);
		} else {
			F4SE::log::warn("Workshop {} stack dump unavailable: stack {}, snapshot {}, debug interface unavailable", reason, _stackID, snapshot);
		}
	}

	void ObservePendingWaits() noexcept
	{
		if (!_waitSnapshotsEnabled || !enableLogging || _loaded || _state.done) { return; }
		try {
			const auto now = Elapsed();
			const auto snapshot = _waitSnapshots.Poll(_state, now, _waitSnapshotsEnabled && enableLogging && !_loaded,
				[this](std::uint32_t index) { return _signals[index] && _signals[index]->Get() == WorkshopCallbacks::Completion::Pending; });
			if (!snapshot) { return; }
			F4SE::log::info("Workshop wait snapshot: stack {}, snapshot {}, threshold {} ms, skipped thresholds {}, elapsed {} ms, oldest row {}, slot {}, generation {}, age {} ms; dispatched {}, returned {}, failed {}, outstanding {}, completed rows {}; non-atomic observations",
				_stackID, snapshot->ordinal, snapshot->thresholdMs, snapshot->skippedThresholds, now,
				snapshot->oldest.row, snapshot->oldest.slot, snapshot->oldest.generation, snapshot->ageMs,
				_state.dispatched, _state.returned, _state.failed, _state.outstanding, _state.completedRows);
			LogPendingRequests("wait", snapshot->ordinal, now);
			DumpPapyrusStacks("wait", snapshot->ordinal);
		} catch (...) {
			// Diagnostics must not cancel a healthy import. The attempt remains
			// consumed even when logging or the debug interface fails.
			try { F4SE::log::warn("Workshop wait snapshot failed: stack {}; import scheduling unchanged", _stackID); } catch (...) {}
		}
	}

	void Finish(RE::BSScript::Variable& result)
	{
		if (_progress) { _progress->Finish(_state.Summary()[0] == 1); }
		if (!_loaded && enableLogging) {
			const auto [done, total] = _progress->Counts();
			CLIPBOARD_DEBUG_LOG(F4SE::log::info("Workshop registration progress: stack {}, completed workshop calls {}, required workshop calls {}; placed/moved half-units per subject object, full callback barrier retained",
				_stackID, done, total));
		}
		_state.elapsedMs = std::max(_state.elapsedMs, Elapsed());
		_spacing.FinishWait(_state.elapsedMs);
		if (_waitSnapshotsEnabled && !_loaded && enableLogging) {
			CLIPBOARD_DEBUG_LOG(F4SE::log::info("Workshop wait snapshot result: stack {}, early enabled {}, attempts {}; pending observations only, final timeout dump independent",
				_stackID, _waitSnapshots.EarlyEnabled(), _waitSnapshots.Attempts()));
		}
		const auto values = _state.Summary();
		ImportSummary summary;
		for (const auto value : values) { summary.push_back(value); }
		if constexpr (RowResults) {
			summary.push_back(ImportRows::CanContinue(_state) ? 1 : 0);
			const auto array = RE::BSScript::get<RE::BSScript::Array>(_references);
			for (std::size_t i = 0; i < _state.rows.size(); ++i) {
				const auto outcome = ImportRows::Classify(_state.rows[i]);
				summary.push_back(outcome);
				if (outcome == ImportRows::Failed || outcome == ImportRows::Pending) {
					auto* ref = array && i < array->size() ? UnpackSafely<RE::TESObjectREFR*>(array->elements[static_cast<std::uint32_t>(i)]).value_or(nullptr) : nullptr;
					LogExcludedImportRow("workshop initialization", i, ref, outcome);
				}
			}
		}
		RE::BSScript::PackVariable(result, std::move(summary));
		if ((_singleCallbackRequested || _callbackLimitRequested >= 0 || _spacing.requestedMs) && !_loaded && enableLogging) {
			CLIPBOARD_DEBUG_LOG(F4SE::log::info("Workshop callback spacing result: stack {}, requested {} ms, effective {} ms, armed {}, held admissions {}, intentional admission idle {} ms (excludes scheduler overshoot), elapsed {} ms, completed rows {}, returned {}, timeout {}",
				_stackID, _spacing.requestedMs, _spacing.effectiveMs, _spacing.armed, _spacing.heldAdmissions,
				_spacing.heldMs, _state.elapsedMs, _state.completedRows, _state.returned, _state.timedOut));
			if (!_latencyComplete) {
				CLIPBOARD_DEBUG_LOG(F4SE::log::info("Workshop callback latency unavailable: stack {}; logging was disabled during successful returns, so partial totals are not reported", _stackID));
			}
			for (std::size_t phase = 0; _latencyComplete && phase < _latency.size(); ++phase) {
				const auto& stats = _latency[phase];
				CLIPBOARD_DEBUG_LOG(F4SE::log::info("Workshop callback latency: stack {}, phase {}, returns {}, total {} ms, maximum {} ms, slowest row {} (zero-based), generation {}; buckets <100/<1000/<10000/<30000/<60000/>=60000 ms: {}/{}/{}/{}/{}/{}; includes queue and signal-observation delay, successful returns only",
					_stackID, PhaseName(static_cast<WorkshopCallbacks::Phase>(phase)), stats.count, stats.totalMs,
					stats.maximumMs, stats.slowest.row, stats.slowest.generation,
					stats.buckets[0], stats.buckets[1], stats.buckets[2], stats.buckets[3], stats.buckets[4], stats.buckets[5]));
			}
		}
		if (_singleCallbackRequested && !_loaded && enableLogging) {
			CLIPBOARD_DEBUG_LOG(F4SE::log::info("Workshop callback admission diagnostic result: stack {}, status {}, effective single {}, admission limit {}, peak outstanding {}, returned {}, omitted {}, failed {}, outstanding {}, completed rows {}, timeout {}, cancelled {}, interrupted {}",
				_stackID, values[0], _state.UsesThrottling() && _state.AdmissionLimit() == 1, _state.AdmissionLimit(), _state.peakOutstanding,
				_state.returned, _state.omittedCalls, _state.failed, _state.outstanding, _state.completedRows,
				_state.timedOut, _state.cancelled, _state.interrupted));
		}
		if (_callbackLimitRequested >= 0 && !_loaded && enableLogging) {
			CLIPBOARD_DEBUG_LOG(F4SE::log::info("Workshop callback limit result: stack {}, requested {}, effective {}, peak outstanding {}, elapsed {} ms, completed rows {}, returned {}, omitted {}, failed {}, outstanding {}, status {}, timeout {}, cancelled {}, interrupted {}",
				_stackID, _callbackLimitRequested, _state.AdmissionLimit(), _state.peakOutstanding, _state.elapsedMs, _state.completedRows,
				_state.returned, _state.omittedCalls, _state.failed, _state.outstanding, values[0], _state.timedOut, _state.cancelled, _state.interrupted));
		}
		if (_callbackResumeRequested >= 0 && !_loaded && enableLogging) {
			const auto& drain = _state.drain;
			CLIPBOARD_DEBUG_LOG(F4SE::log::info("Workshop callback drain result: stack {}, requested resume at {}, effective {}, high {}, resume at {}, closures {}, reopenings {}, closed {}, admission held {} ms, transition details {}, outstanding {}, returned {}, completed rows {}, timeout {}, cancelled {}, interrupted {}; empty group is not full initialization completion",
				_stackID, _callbackResumeRequested, drain.Enabled(), drain.High(), drain.ResumeAt(), drain.Closures(),
				drain.Reopenings(), drain.Closed(), drain.HeldMs(_state.elapsedMs), _drainTransitionLines, _state.outstanding,
				_state.returned, _state.completedRows, _state.timedOut, _state.cancelled, _state.interrupted));
		}
		if (values[0] != 1) {
			// Failure handling below retains ownership of uncertain callbacks.
			F4SE::log::warn("Workshop initialization stack {} incomplete: status {}, failed {}, outstanding {}, timeout {}, interrupted {}, throttling {}, returned {}/{}, elapsed {} ms",
				_stackID, values[0], _state.failed, _state.outstanding, _state.timedOut, _state.interrupted,
				_state.UsesThrottling(), _state.returned, _state.planned, _state.elapsedMs);
		}
		// Capture the actual callee stacks before resuming Papyrus into the
		// incomplete-import dialog. This only observes the VM; never cancel,
		// replay or acknowledge a timed-out call on the strength of a dump.
		if (_state.timedOut && _state.outstanding && !_loaded && !_timeoutStacksDumped && enableLogging) {
			_timeoutStacksDumped = true;
			LogPendingRequests("timeout", 0, _state.elapsedMs);
			DumpPapyrusStacks("timeout", 0);
		}
		// Real pending VM callbacks retain their own lease until return/cancel.
		_lease.reset();
	}

	RE::BSScript::Variable _tool;
	RE::BSScript::Variable _references;
	RE::BSScript::Variable _workshop;
	bool _useThrottlingRequested{ true };
	bool _singleCallbackRequested{};
	std::int32_t _callbackLimitRequested{ -1 };
	std::int32_t _callbackResumeRequested{ -1 };
	std::uint64_t _drainTransitionSeen{};
	std::uint32_t _drainTransitionLines{};
	WorkshopCallbacks::State _state;
	WorkshopCallbacks::CallbackSpacing _spacing;
	std::optional<std::uint32_t> _spacingCheckRow;
	std::uint64_t _spacingCheckDeadline{};
	std::array<WorkshopCallbacks::CallbackLatency, 4> _latency;
	bool _latencyComplete{ true };
	std::vector<std::shared_ptr<WorkshopCallbacks::CompletionSignal>> _signals;
	std::vector<DispatchInfo> _dispatchInfo;
	std::shared_ptr<ImportLease> _lease;
	std::chrono::steady_clock::time_point _start;
	std::shared_ptr<ImportProgress::RegistrationProgress> _progress;
	std::uint32_t _progressOwner{};
	bool _progressStarted{};
	std::size_t _progressCursor{};
	// Runtime observations only; restored jobs remain interrupted, and this
	// schedule adds no serialized fields; v5 widens the permitted slot count.
	WorkshopCallbacks::WaitSnapshotSchedule _waitSnapshots;
	WorkshopNoOp::Resources _resources;
	bool _started{};
	bool _loaded{};
	bool _timeoutStacksDumped{};
	bool _waitSnapshotsEnabled{};
};
using InitializeImportedWorkshopObjectsFunctor = WorkshopInitializationFunctor<false>;
using InitializeImportedWorkshopRowsFunctor = WorkshopInitializationFunctor<true>;
