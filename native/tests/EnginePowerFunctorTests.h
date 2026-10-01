// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "PowerTaskState.h"
#include <algorithm>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

// Compile the production coordinator template against a controllable engine
// queue and numeric VM/serialization boundary. This tests the real coordinator,
// rather than reimplementing its publish/consume decisions in the fixture.
namespace Clipboard::PowerCoordinatorTests
{
    namespace RE::BSScript
    {
        struct Object { std::uint64_t GetHandle() const { return 19; } };
        struct Variable {
            int value{};
            Variable& operator=(std::nullptr_t) { value = 0; return *this; }
            template<class T> bool is() const { return false; }
        };
        template<class T> std::shared_ptr<T> get(const Variable&) { return {}; }
    }
    namespace F4SE
    {
        struct SerializationTag {};
        struct SerializationInterface { mutable unsigned progress{}; };
        namespace log { template<class... Args> void error(const char*, Args&&...) {} }
    }
    namespace EngineTaskDispatch
    {
        inline std::deque<std::function<void()>> queue;
        inline bool accept{ true }, inTask{};
        inline bool Queue(std::function<void()> action) {
            if (!accept) { return false; }
            queue.push_back(std::move(action)); return true;
        }
        template<class T> void Retire(T* value) {
            if (inTask) { delete value; }
            else { queue.push_back([value] { delete value; }); }
        }
        inline void RunOne() {
            auto action = std::move(queue.front()); queue.pop_front();
            inTask = true; action(); action = {}; inTask = false;
        }
        inline void Drain() { while (!queue.empty()) { RunOne(); } }
    }
    enum class RegistrationState { kReady, kUnavailable };
    inline std::atomic<RegistrationState> g_registrationState{ RegistrationState::kReady };
    class LatentFunctorBase
    {
    public:
        explicit LatentFunctorBase(F4SE::SerializationTag) {}
        explicit LatentFunctorBase(std::uint32_t stack) : _stackID(stack) {}
        virtual ~LatentFunctorBase() = default;
        virtual const char* ClassName() const = 0;
        virtual std::uint32_t ClassVersion() const = 0;
        virtual bool Save(const F4SE::SerializationInterface*) = 0;
        virtual bool Load(const F4SE::SerializationInterface*, std::uint32_t) = 0;
        virtual bool ShouldReschedule(std::int32_t&) = 0;
        virtual bool ShouldResumeStack(std::uint32_t&) = 0;
        virtual bool Run(RE::BSScript::Variable&) = 0;
    protected:
        std::uint32_t _stackID{};
    };

    #include "EnginePowerFunctor.inl"

    struct Observation
    {
        unsigned calls{}, aborted{}, released{}, releasedOnEngine{}, copied{}, copiedOnEngine{};
        bool throwDuringRun{};
    };
    class Worker final : public LatentFunctorBase
    {
    public:
        explicit Worker(F4SE::SerializationTag tag) : LatentFunctorBase(tag) {}
        Worker(std::uint32_t stack, std::shared_ptr<Observation> observed) : LatentFunctorBase(stack), _observed(std::move(observed)) {}
        ~Worker() override {
            if (_observed) { ++_observed->released; _observed->releasedOnEngine += EngineTaskDispatch::inTask; }
        }
        const char* ClassName() const override { return "power-fixture"; }
        std::uint32_t ClassVersion() const override { return 2; }
        bool Save(const F4SE::SerializationInterface* output) override { output->progress = _progress; return true; }
        bool Load(const F4SE::SerializationInterface* input, std::uint32_t) override { _progress = input->progress; _stackID = 77; return true; }
        bool ShouldReschedule(std::int32_t& delay) override { delay = _progress == 1 ? 500 : 1; return _progress < 2; }
        bool ShouldResumeStack(std::uint32_t& stack) override { stack = _stackID; return _progress == 2; }
        bool Run(RE::BSScript::Variable& result) override {
            ++_observed->calls;
            if (_observed->throwDuringRun) { throw 1; }
            ++_progress; result.value = static_cast<int>(_progress); return true;
        }
        std::uint64_t PowerToolHandle() const { return 19; }
        void SetPowerCancellation(std::function<bool()> test) { _cancelled = std::move(test); }
        void AbortPowerTask() noexcept { if (!_aborted) { ++_observed->aborted; _aborted = true; } }
        static void PowerFailureResult(RE::BSScript::Variable& result) { result.value = -1; }
        void CopyPowerCheckpoint(Worker& target, bool) {
            target._stackID = _stackID; target._progress = _progress;
            ++_observed->copied; _observed->copiedOnEngine += EngineTaskDispatch::inTask;
        }
    private:
        std::shared_ptr<Observation> _observed;
        std::function<bool()> _cancelled;
        unsigned _progress{};
        bool _aborted{};
    };
}

template<class Check> void CheckEnginePowerFunctor(Check&& check)
{
    using namespace Clipboard::PowerCoordinatorTests;
    using Functor = EnginePowerFunctor<Worker>;
    {
        auto observed = std::make_shared<Observation>();
        Functor coordinator(42, observed);
        Clipboard::PowerCoordinatorTests::F4SE::SerializationInterface saved;
        Clipboard::PowerCoordinatorTests::RE::BSScript::Variable result;
        std::uint32_t stack{};
        std::int32_t delay{};
        coordinator.Run(result);
        coordinator.Run(result);
        check(EngineTaskDispatch::queue.size() == 1 && !coordinator.ShouldResumeStack(stack),
            "real power coordinator polls one pending task without overlap or early completion");
        coordinator.Save(&saved);
        check(saved.progress == 0 && observed->calls == 0, "real coordinator saves initial checkpoint before engine execution");
        EngineTaskDispatch::RunOne();
        coordinator.Save(&saved);
        check(saved.progress == 0 && observed->calls == 1, "published but unconsumed work does not change the saved checkpoint");
        coordinator.Run(result);
        coordinator.Save(&saved);
        check(saved.progress == 1 && EngineTaskDispatch::queue.empty() && coordinator.ShouldReschedule(delay) && delay == 500,
            "real coordinator consumes progress and honors worker delay before resubmission");
        coordinator.Run(result);
        check(EngineTaskDispatch::queue.size() == 1, "real coordinator submits the next bounded slice after its retry wait");
        EngineTaskDispatch::RunOne();
        coordinator.Run(result);
        check(coordinator.ShouldResumeStack(stack) && stack == 42 && result.value == 2 && !coordinator.ShouldReschedule(delay),
            "real coordinator resumes the original stack only after consuming the final result");
        EngineTaskDispatch::Drain();
        check(observed->released == 1 && observed->releasedOnEngine == 1 && observed->copiedOnEngine == 0,
            "worker resources retire on engine queue and checkpoints copy only on coordinator");
    }
    {
        auto observed = std::make_shared<Observation>();
        auto coordinator = std::make_unique<Functor>(42, observed);
        Clipboard::PowerCoordinatorTests::RE::BSScript::Variable result;
        coordinator->Run(result);
        coordinator.reset();
        check(observed->released == 0, "destroyed serialized owner leaves a queued worker retained");
        EngineTaskDispatch::Drain();
        check(observed->calls == 0 && observed->aborted == 1 && observed->releasedOnEngine == 1,
            "orphaned real task cleans up on engine queue without entering worker VM access");
    }
    {
        auto observed = std::make_shared<Observation>();
        Functor coordinator(42, observed);
        Clipboard::PowerCoordinatorTests::RE::BSScript::Variable result;
        std::uint32_t stack{};
        coordinator.Run(result);
        CancelPowerTasks(19);
        EngineTaskDispatch::RunOne();
        coordinator.Run(result);
        check(coordinator.ShouldResumeStack(stack) && result.value == -1 && observed->calls == 0,
            "tool cancellation is consumed as failure without executing queued power work");
        EngineTaskDispatch::Drain();
    }
    {
        auto observed = std::make_shared<Observation>();
        Functor coordinator(42, observed);
        Clipboard::PowerCoordinatorTests::RE::BSScript::Variable result;
        Clipboard::PowerCoordinatorTests::F4SE::SerializationInterface saved;
        std::uint32_t stack{};
        coordinator.Run(result);
        coordinator.Save(&saved);
        ResetPowerTasks();
        Functor restored(Clipboard::PowerCoordinatorTests::F4SE::SerializationTag{});
        check(restored.Load(&saved, 2), "real coordinator accepts the legacy saved worker payload");
        restored.Run(result);
        check(restored.ShouldResumeStack(stack) && stack == 77 && result.value == -1 && EngineTaskDispatch::queue.size() == 1,
            "restored power call fails closed without adding replay work");
        EngineTaskDispatch::RunOne();
        coordinator.Run(result);
        check(coordinator.ShouldResumeStack(stack) && result.value == -1 && observed->calls == 0,
            "old-generation queued task is drained without VM worker access after load");
        EngineTaskDispatch::Drain();
    }
    {
        auto observed = std::make_shared<Observation>();
        Functor coordinator(42, observed);
        Clipboard::PowerCoordinatorTests::RE::BSScript::Variable result;
        std::uint32_t stack{};
        EngineTaskDispatch::accept = false;
        coordinator.Run(result);
        check(!coordinator.ShouldResumeStack(stack) && observed->calls == 0, "queue rejection does not execute fallback work or invent an immediate result");
        coordinator.Run(result);
        check(coordinator.ShouldResumeStack(stack) && result.value == -1 && observed->calls == 0,
            "real coordinator consumes terminal submission failure without worker fallback");
        EngineTaskDispatch::accept = true;
        EngineTaskDispatch::Drain();
    }
    {
        auto observed = std::make_shared<Observation>(); observed->throwDuringRun = true;
        Functor coordinator(42, observed);
        Clipboard::PowerCoordinatorTests::RE::BSScript::Variable result;
        std::uint32_t stack{};
        coordinator.Run(result);
        EngineTaskDispatch::RunOne();
        coordinator.Run(result);
        check(coordinator.ShouldResumeStack(stack) && result.value == -1 && observed->aborted == 1,
            "worker exceptions publish abort completion instead of stranding the Papyrus stack");
        EngineTaskDispatch::Drain();
    }
    check(EngineTaskDispatch::queue.empty(), "all coordinator test tasks and deferred cleanup have drained");
}
