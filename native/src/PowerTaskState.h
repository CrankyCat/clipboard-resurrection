// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace Clipboard::PowerTask
{
    // This domain must outlive its jobs. Reset/load invalidates already queued
    // jobs without reaching into their engine-owned mutable state.
    class Epoch
    {
    public:
        std::uint64_t Capture() const noexcept { return _value.load(std::memory_order_acquire); }
        void Invalidate() noexcept { _value.fetch_add(1, std::memory_order_acq_rel); }

    private:
        std::atomic<std::uint64_t> _value{ 1 };
    };

    struct Ticket
    {
        std::uint64_t value{};
        explicit operator bool() const noexcept { return value != 0; }
    };

    // The coordinator owns its saved checkpoint. The queued task owns mutable
    // work/results. Only Consume permits copying those results into the saved
    // checkpoint; neither Pending nor Ready allows another task to be queued.
    // Tickets also prevent an old task from publishing into a later slice.
    class Gate
    {
    public:
        explicit Gate(const Epoch& epoch) noexcept : _epoch(epoch), _generation(epoch.Capture()) {}

        Ticket TryQueue() noexcept
        {
            auto state = _state.load(std::memory_order_acquire);
            if (PhaseOf(state) != Phase::Idle || Cancelled() ||
                SerialOf(state) == (std::numeric_limits<std::uint64_t>::max() >> kPhaseBits)) {
                return {};
            }
            const Ticket ticket{ SerialOf(state) + 1 };
            return _state.compare_exchange_strong(state, Encode(ticket, Phase::Pending),
                std::memory_order_acq_rel) ? ticket : Ticket{};
        }

        // Call before unpacking VM references and between engine operations.
        // Cancellation is cooperative: a call already executing cannot be
        // preempted, but no later operation should use the canceled job.
        bool CanRun(Ticket ticket) const noexcept
        {
            return ticket && _state.load(std::memory_order_acquire) == Encode(ticket, Phase::Pending) &&
                !Cancelled();
        }

        // Publish even a canceled slice, after its engine-side cleanup. The
        // coordinator must consume that completion before releasing its owner.
        bool Publish(Ticket ticket) noexcept
        {
            if (!ticket) { return false; }
            auto expected = Encode(ticket, Phase::Pending);
            return _state.compare_exchange_strong(expected, Encode(ticket, Phase::Ready),
                std::memory_order_release, std::memory_order_relaxed);
        }

        // Use only when submission was rejected and no task was accepted. There
        // is deliberately no worker-thread fallback. Failure is terminal and is
        // itself consumed exactly once, like a completed slice.
        bool FailSubmission(Ticket ticket) noexcept
        {
            if (!ticket) { return false; }
            auto expected = Encode(ticket, Phase::Pending);
            return _state.compare_exchange_strong(expected, Encode(ticket, Phase::SubmissionFailed),
                std::memory_order_release, std::memory_order_relaxed);
        }

        bool Consume() noexcept
        {
            auto state = _state.load(std::memory_order_acquire);
            const auto phase = PhaseOf(state);
            if (phase != Phase::Ready && phase != Phase::SubmissionFailed) { return false; }
            const auto next = phase == Phase::Ready ? Phase::Idle : Phase::FailedIdle;
            return _state.compare_exchange_strong(state, Encode(Ticket{ SerialOf(state) }, next),
                std::memory_order_acq_rel);
        }

        void Cancel() noexcept { _cancelled.store(true, std::memory_order_release); }
        bool Cancelled() const noexcept
        {
            return _cancelled.load(std::memory_order_acquire) || _epoch.Capture() != _generation;
        }
        bool Failed() const noexcept
        {
            const auto phase = PhaseOf(_state.load(std::memory_order_acquire));
            return phase == Phase::SubmissionFailed || phase == Phase::FailedIdle;
        }
        bool Outstanding() const noexcept
        {
            const auto phase = PhaseOf(_state.load(std::memory_order_acquire));
            return phase == Phase::Pending || phase == Phase::Ready || phase == Phase::SubmissionFailed;
        }

    private:
        enum class Phase : std::uint64_t { Idle, Pending, Ready, SubmissionFailed, FailedIdle };
        static constexpr unsigned kPhaseBits = 3;
        static constexpr std::uint64_t kPhaseMask = (1U << kPhaseBits) - 1;
        static constexpr Phase PhaseOf(std::uint64_t value) noexcept
        {
            return static_cast<Phase>(value & kPhaseMask);
        }
        static constexpr std::uint64_t SerialOf(std::uint64_t value) noexcept { return value >> kPhaseBits; }
        static constexpr std::uint64_t Encode(Ticket ticket, Phase phase) noexcept
        {
            return (ticket.value << kPhaseBits) | static_cast<std::uint64_t>(phase);
        }

        const Epoch& _epoch;
        const std::uint64_t _generation;
        std::atomic<std::uint64_t> _state{};
        std::atomic_bool _cancelled{};
    };
}
