// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace Clipboard::LegacyCleanup
{
	// Process-local exclusion, never serialized. Jobs hold tickets from allocation
	// through destruction; callbacks hold independent tickets until the actual VM
	// return/cancellation. Cancelling an import therefore cannot make cleanup idle.
	class Gate
	{
	public:
		explicit Gate(std::int32_t seed = 0) : _next(seed > 0 ? seed : 0) {}
		void StartWork()
		{
			std::scoped_lock lock(_lock);
			if (_active) { throw std::runtime_error("Clipboard cleanup is in progress"); }
			++_work;
		}
		void FinishWork() noexcept
		{
			std::scoped_lock lock(_lock);
			if (_work) { --_work; }
		}
		[[nodiscard]] std::int32_t Begin()
		{
			std::scoped_lock lock(_lock);
			if (_active || _work || _uncertain || _next == std::numeric_limits<std::int32_t>::max()) { return 0; }
			_active = ++_next;
			return _active;
		}
		[[nodiscard]] bool Active(std::int32_t token = 0) const
		{
			std::scoped_lock lock(_lock);
			return _active > 0 && (token == 0 || token == _active);
		}
		void End(std::int32_t token)
		{
			std::scoped_lock lock(_lock);
			if (token > 0 && token == _active) { _active = 0; }
		}
		void MarkUncertain()
		{
			std::scoped_lock lock(_lock);
			_uncertain = true;
		}
		void ResetForLoad()
		{
			std::scoped_lock lock(_lock);
			_active = 0;
			_uncertain = false;
			// Old tickets must decrement themselves, including callbacks retained
			// by the old VM. Never zero the count or reuse the previous token.
		}
	private:
		mutable std::mutex _lock;
		std::uint64_t _work{};
		std::int32_t _next{};
		std::int32_t _active{};
		bool _uncertain{};
	};

	class WorkTicket
	{
	public:
		WorkTicket() noexcept = default;
		explicit WorkTicket(Gate& gate) { Start(gate); }
		~WorkTicket() { Complete(); }
		WorkTicket(const WorkTicket&) = delete;
		WorkTicket& operator=(const WorkTicket&) = delete;
		void Start(Gate& gate)
		{
			if (_gate) { throw std::logic_error("Work ticket already owns an operation"); }
			gate.StartWork();
			_gate = &gate;
		}
		void Complete() noexcept
		{
			if (_gate) { _gate->FinishWork(); _gate = nullptr; }
		}
	private:
		Gate* _gate{};
	};
}
