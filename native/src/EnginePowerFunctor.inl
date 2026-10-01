// SPDX-License-Identifier: GPL-3.0-or-later
// Included after VM serialization helpers and LatentFunctorBase.

PowerTask::Epoch g_powerTaskEpoch;
std::mutex g_powerTaskLock;
std::unordered_multimap<std::uint64_t, std::weak_ptr<PowerTask::Gate>> g_powerTasks;

std::uint64_t ImportToolHandle(const RE::BSScript::Variable& value)
{
    if (!value.is<RE::BSScript::Object>()) { return 0; }
    const auto object = RE::BSScript::get<RE::BSScript::Object>(value);
    return object ? object->GetHandle() : 0;
}

void CancelPowerTasks(std::uint64_t handle)
{
    std::scoped_lock lock(g_powerTaskLock);
    const auto [begin, end] = g_powerTasks.equal_range(handle);
    for (auto it = begin; it != end; ++it) {
        if (const auto gate = it->second.lock()) { gate->Cancel(); }
    }
}

void ResetPowerTasks()
{
    // Invalidates queued slices before any old VM handles can be unpacked.
    g_powerTaskEpoch.Invalidate();
    std::scoped_lock lock(g_powerTaskLock);
    g_powerTasks.clear();
}

template <class Worker>
class EnginePowerFunctor final : public LatentFunctorBase
{
    struct Job
    {
        std::unique_ptr<Worker> worker;
        std::shared_ptr<PowerTask::Gate> gate{ std::make_shared<PowerTask::Gate>(g_powerTaskEpoch) };
        RE::BSScript::Variable result;
        bool done{};
        std::int32_t delay{ 1 };

        explicit Job(std::unique_ptr<Worker> value) : worker(std::move(value))
        {
            worker->SetPowerCancellation([state = gate] { return state->Cancelled(); });
        }
        ~Job()
        {
            // Retire guarantees engine-task destruction even if the coordinator
            // disappears with an idle/completed slice or submission fails.
            if (gate->Cancelled() || gate->Failed()) { worker->AbortPowerTask(); }
        }

        void RunSlice(PowerTask::Ticket ticket) noexcept
        {
            try {
                if (!gate->CanRun(ticket) || g_registrationState.load(std::memory_order_acquire) != RegistrationState::kReady) {
                    gate->Cancel();
                } else {
                    worker->Run(result);
                    std::uint32_t stack{};
                    done = worker->ShouldResumeStack(stack);
                    worker->ShouldReschedule(delay);
                }
                if (gate->Cancelled()) { Abort(); }
            } catch (...) {
                gate->Cancel();
                Abort();
            }
            gate->Publish(ticket);
        }

        void Abort() noexcept
        {
            done = true;
            worker->AbortPowerTask();
            try { Worker::PowerFailureResult(result); } catch (...) { result = nullptr; }
        }
    };

public:
    explicit EnginePowerFunctor(F4SE::SerializationTag tag) : LatentFunctorBase(tag), _checkpoint(std::make_unique<Worker>(tag)) {}

    template <class... Args>
    explicit EnginePowerFunctor(std::uint32_t stack, Args&&... args) : LatentFunctorBase(stack),
        _checkpoint(std::make_unique<Worker>(F4SE::SerializationTag{}))
    {
        auto worker = std::make_unique<Worker>(stack, std::forward<Args>(args)...);
        worker->CopyPowerCheckpoint(*_checkpoint, true);
        _job = std::shared_ptr<Job>(new Job(std::move(worker)), [](Job* job) { EngineTaskDispatch::Retire(job); });
        std::scoped_lock lock(g_powerTaskLock);
        std::erase_if(g_powerTasks, [](const auto& entry) { return entry.second.expired(); });
        g_powerTasks.emplace(_checkpoint->PowerToolHandle(), _job->gate);
    }

    ~EnginePowerFunctor() override { if (_job) { _job->gate->Cancel(); } }
    const char* ClassName() const override { return _checkpoint->ClassName(); }
    std::uint32_t ClassVersion() const override { return _checkpoint->ClassVersion(); }
    bool Save(const F4SE::SerializationInterface* intfc) override
    {
        // No worker reads while a task is pending. Only consumed numeric/VM
        // state is saved; engine pointers and task state never enter co-saves.
        return _checkpoint->Save(intfc);
    }
    bool Load(const F4SE::SerializationInterface* intfc, std::uint32_t version) override
    {
        _loaded = true;
        return _checkpoint->Load(intfc, version);
    }
    bool ShouldReschedule(std::int32_t& delay) override { delay = _delay; return !_done; }
    bool ShouldResumeStack(std::uint32_t& stack) override
    {
        _checkpoint->ShouldResumeStack(stack);
        return _done;
    }

    bool Run(RE::BSScript::Variable& result) override
    {
        try {
            // Restored latent calls must not replay wires or power mutations.
            if (_loaded || !_job) {
                _done = true;
                Worker::PowerFailureResult(result);
                return true;
            }
            if (_job->gate->Consume()) {
                if (_job->gate->Cancelled() || _job->gate->Failed()) {
                    _done = true;
                    Worker::PowerFailureResult(result);
                } else {
                    _job->worker->CopyPowerCheckpoint(*_checkpoint, false);
                    _done = _job->done;
                    _delay = std::max<std::int32_t>(1, _job->delay);
                    if (_done) { result = _job->result; }
                }
                if (_done) { _job.reset(); return true; }
                // Honor the worker's retry delay before submitting the next
                // slice, rather than delaying only the subsequent result poll.
                return true;
            }
            if (const auto ticket = _job->gate->TryQueue()) {
                _delay = 1;
                if (!EngineTaskDispatch::Queue([job = _job, ticket] { job->RunSlice(ticket); })) {
                    _job->gate->FailSubmission(ticket);
                    F4SE::log::error("Clipboard power stage {} could not queue an engine task; stopping without VM-worker fallback", ClassName());
                }
            } else if (_job->gate->Cancelled() && !_job->gate->Outstanding()) {
                _done = true;
                Worker::PowerFailureResult(result);
                _job.reset();
            }
        } catch (...) {
            if (_job) { _job->gate->Cancel(); }
            _done = true;
            try { Worker::PowerFailureResult(result); } catch (...) { result = nullptr; }
            _job.reset();
        }
        return true;
    }

private:
    std::unique_ptr<Worker> _checkpoint;
    std::shared_ptr<Job> _job;
    bool _loaded{}, _done{};
    std::int32_t _delay{ 1 };
};
