// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
// Included after ImportFunctors.inl; uses its cancellable per-tool lease.

std::weak_ptr<ImportLease> g_pacedScrapLease; // guarded by g_importLock

// Owned independently of the serialized coordinator. Never capture a functor
// pointer in the unsaveable F4SE task queue: load/revert can destroy it first.
struct PacedScrapTask
{
    PerformanceBatch::SliceHandoff handoff;
    PerformanceBatch::Progress progress;
    RE::BSScript::Variable tool, references;
    std::shared_ptr<ImportLease> lease;
    std::optional<ScrapBatch> batch;
    std::uint32_t stackID{}, playerCellID{};
    bool started{}, done{}, cancelled{};

    void RunSlice() noexcept
    {
        const auto start = std::chrono::steady_clock::now();
        const auto elapsed = [&] {
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count());
        };
        try {
            // Revert cancels the lease before stale VM values or references
            // can be unpacked by a task left in F4SE's queue.
            if (!lease || lease->cancelled.load()) { done = cancelled = true; }
            if (!done) {
                auto* ownerTool = UnpackSafely<RE::TESObjectREFR*>(tool).value_or(nullptr);
                const auto* player = RE::PlayerCharacter::GetSingleton();
                if (!ownerTool || ownerTool->IsDeleted() || !player || !player->parentCell ||
                    (started && player->parentCell->formID != playerCellID)) { done = cancelled = true; }
                if (!done && !started) {
                    auto rows = UnpackSafely<ReferenceArray>(references);
                    if (!rows) { done = cancelled = true; }
                    else {
                        playerCellID = player->parentCell->formID;
                        batch.emplace(g_callbacks.snapshotScrap(*rows));
                        progress.total = static_cast<std::uint32_t>(batch->Targets().size());
                        started = true;
                    }
                }
            }
            if (!done) {
                PerformanceBatch::VisitSlice(progress, true, [&](std::uint32_t row) {
                    if (lease->cancelled.load()) { done = cancelled = true; return; }
                    const auto& target = batch->Targets()[row];
                    auto* ref = target.reference.get();
                    if (!ref || ref->IsDeleted()) { ++progress.skipped; }
                    else {
                        auto* workshop = target.workshop;
                        if (workshop && workshop->IsDeleted()) { workshop = nullptr; }
                        if (g_callbacks.scrapTarget(stackID, ref, workshop)) { ++progress.dispatched; }
                        else { ++progress.failed; }
                    }
                }, [&] { return cancelled ? PerformanceBatch::kBudgetMicroseconds : elapsed(); });
                done = done || progress.cursor == progress.total;
            }
        } catch (...) { done = cancelled = true; }
        // Final intrusive-reference release also occurs on the engine task
        // queue. A cancelled pending task owns its state until reaching here.
        if (done) { batch.reset(); }
        handoff.Publish();
    }
};

template<bool Scrap>
class PerformanceBatchFunctor final : public LatentFunctorBase
{
public:
    explicit PerformanceBatchFunctor(F4SE::SerializationTag tag) : LatentFunctorBase(tag) {}
    PerformanceBatchFunctor(std::uint32_t stackID, RE::TESObjectREFR* tool, ReferenceArray rows) : LatentFunctorBase(stackID)
    {
        if (rows.size() > PerformanceBatch::kMaximumRows) { throw std::length_error("Performance batch row limit"); }
        _progress.total = static_cast<std::uint32_t>(rows.size());
        RE::BSScript::PackVariable(_tool, tool);
        RE::BSScript::PackVariable(_references, std::move(rows));
    }
    ~PerformanceBatchFunctor() override
    {
        if constexpr (Scrap) {
            // An already queued slice must not outlive its tool/job as active
            // work, including when the functor is removed during revert.
            if (_lease && !_done) { _lease->cancelled.store(true); }
        }
    }
    const char* ClassName() const override { return Scrap ? kScrapObjectsPacedName : kGetImportedAnimationCandidatesName; }
    std::uint32_t ClassVersion() const override { return 1; }

    bool Save(const F4SE::SerializationInterface* intfc) override
    {
        try {
            return SaveStack(intfc) && WriteVariable(intfc, _tool) && WriteVariable(intfc, _references) &&
                _progress.Save([&](const auto& value) { return WriteExact(intfc, value); });
        } catch (...) { return false; }
    }
    bool Load(const F4SE::SerializationInterface* intfc, std::uint32_t version) override
    {
        try {
            if (version != 1 || !LoadStack(intfc) || !ReadVariable(intfc, _tool) ||
                !ReadVariable(intfc, _references) || !_references.is<RE::BSScript::Array>()) { return false; }
            const auto array = RE::BSScript::get<RE::BSScript::Array>(_references);
            if (!array || !_progress.Load([&](auto& value) { return ReadExact(intfc, value); }, array->size())) { return false; }
            _done = true;
            _cancelled = true;
            return true;
        } catch (...) { return false; }
    }
    bool ShouldReschedule(std::int32_t& delay) override
    {
        delay = Scrap ? PerformanceBatch::kScrapDelayMs : 1;
        return !_done;
    }
    bool ShouldResumeStack(std::uint32_t& stackID) override { stackID = _stackID; return _done; }

    bool Run(RE::BSScript::Variable& result) override
    {
        if constexpr (Scrap) { return RunScrap(result); }
        else {
        try {
            const auto start = std::chrono::steady_clock::now();
            if (_start == std::chrono::steady_clock::time_point{}) { _start = start; }
            auto* tool = UnpackSafely<RE::TESObjectREFR*>(_tool).value_or(nullptr);
            if (!_done && (!_started ? !Begin(tool) : !tool || tool->IsDeleted() || !_lease || _lease->cancelled.load())) {
                _done = _cancelled = true;
            }
            const auto array = _references.is<RE::BSScript::Array>() ? RE::BSScript::get<RE::BSScript::Array>(_references) : nullptr;
            if (!_done && !array) { _done = _cancelled = true; }
            if (!_done) { PerformanceBatch::VisitSlice(_progress, false, [&](std::uint32_t row) {
                auto* ref = UnpackSafely<RE::TESObjectREFR*>(array->elements[row]).value_or(nullptr);
                if (!ref) { ++_progress.skipped; }
                else {
                    const auto kind = ImportAnimation::GetKind(ref);
                    PerformanceBatch::Classify(row, kind, _pairs, _progress.failed);
                    if (kind >= 0 && kind <= 3) { ++_progress.dispatched; }
					// Non-animated objects finish their animation unit at classification;
					// candidates finish only after the Papyrus animation call returns.
					if (kind == 0 && _powerProgress) { _powerProgress->Complete(row, 2); }
                }
            }, [&] { return Microseconds(start); }); }
            _done = _done || _progress.cursor == _progress.total;
            if (_done) { Finish(result); }
            return true;
        } catch (...) {
            _done = _cancelled = true;
            try { Finish(result); } catch (...) { result = nullptr; _lease.reset(); }
            return true;
        }
        }
    }
private:
    bool RunScrap(RE::BSScript::Variable& result)
    {
        try {
            if (_start == std::chrono::steady_clock::time_point{}) { _start = std::chrono::steady_clock::now(); }
            if (!_done && !_started) {
                auto* tool = UnpackSafely<RE::TESObjectREFR*>(_tool).value_or(nullptr);
                if (!Begin(tool)) { _done = _cancelled = true; }
            }
            if (!_done && !_scrapTask) {
                _scrapTask = std::make_shared<PacedScrapTask>();
                _scrapTask->tool = _tool;
                _scrapTask->references = _references;
                _scrapTask->lease = _lease;
                _scrapTask->progress = _progress;
                _scrapTask->stackID = _stackID;
            }
            if (!_done && _scrapTask->handoff.Consume()) {
                // Only completed task state is read here or included in Save.
                _progress = _scrapTask->progress;
                _done = _scrapTask->done;
                _cancelled = _scrapTask->cancelled;
            }
            if (_done) { Finish(result); }
            else if (_scrapTask->handoff.TryQueue()) {
                // F4SE AddTask executes after MessageQueueProcessTask, alongside
                // queued workshop power updates. DelayFunctorManager runs on a
                // different VM worker and must never call ScrapReference here.
                const auto* tasks = F4SE::GetTaskInterface();
                if (!tasks || tasks->Version() < F4SE::TaskInterface::kVersion) {
                    throw std::runtime_error("Paced scrap requires the F4SE task interface");
                }
                tasks->AddTask([job = _scrapTask] { job->RunSlice(); });
            }
        } catch (...) {
            if (_lease) { _lease->cancelled.store(true); }
            _done = _cancelled = true;
            try { Finish(result); } catch (...) { result = nullptr; _scrapTask.reset(); _lease.reset(); }
        }
        return true;
    }

    static std::uint64_t Microseconds(std::chrono::steady_clock::time_point start)
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
    }
    std::uint64_t Milliseconds() const { return Microseconds(_start) / 1000; }
    bool Begin(RE::TESObjectREFR* tool)
    {
        _started = true;
		if constexpr (!Scrap) { _powerProgress = FindPowerProgress(_tool); }
        if (!tool || tool->IsDeleted() || g_registrationState.load() != RegistrationState::kReady) { return false; }
        const auto handle = ImportToolHandle(_tool);
        if (!handle) { return false; }
        {
            std::scoped_lock lock(g_importLock);
            auto& active = g_importLeases[handle];
            if (active.lock() || (Scrap && g_pacedScrapLease.lock())) { return false; }
            _lease = std::make_shared<ImportLease>();
            active = _lease;
            if constexpr (Scrap) { g_pacedScrapLease = _lease; }
        }
        return true;
    }
    void Finish(RE::BSScript::Variable& result)
    {
        ImportSummary summary;
        const auto elapsed = static_cast<std::int32_t>(std::min<std::uint64_t>(Milliseconds(), INT32_MAX));
        const std::int32_t status = _cancelled ? -1 : (Scrap && _progress.failed ? 0 : 1);
        if constexpr (Scrap) {
            for (const auto value : { status, static_cast<std::int32_t>(_progress.total), static_cast<std::int32_t>(_progress.cursor),
                static_cast<std::int32_t>(_progress.dispatched), static_cast<std::int32_t>(_progress.skipped),
                static_cast<std::int32_t>(_progress.failed), elapsed }) { summary.push_back(value); }
        } else {
            for (const auto value : { status, static_cast<std::int32_t>(_progress.total), static_cast<std::int32_t>(_progress.failed), elapsed }) { summary.push_back(value); }
            if (!_cancelled) { for (const auto value : _pairs) { summary.push_back(value); } }
        }
        if (status != 1 || _progress.failed) {
            F4SE::log::warn("{} stack {} incomplete: status {}, failed/unavailable {}, interrupted {}",
                ClassName(), _stackID, status, _progress.failed, _progress.interrupted);
        }
        _scrapTask.reset();
        _lease.reset();
        RE::BSScript::PackVariable(result, std::move(summary));
    }
    RE::BSScript::Variable _tool, _references;
    PerformanceBatch::Progress _progress;
    std::shared_ptr<PacedScrapTask> _scrapTask;
    std::vector<std::int32_t> _pairs;
    std::shared_ptr<ImportLease> _lease;
	std::shared_ptr<PowerProgressSession> _powerProgress;
    std::chrono::steady_clock::time_point _start{};
    bool _started{}, _done{}, _cancelled{};
};
using ScrapObjectsPacedFunctor = PerformanceBatchFunctor<true>;
using GetImportedAnimationCandidatesFunctor = PerformanceBatchFunctor<false>;
