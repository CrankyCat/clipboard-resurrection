// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <atomic>
#include <utility>

namespace Clipboard::StartupCallbacks
{
	class Gate
	{
	public:
		[[nodiscard]] bool IsEnabled() const noexcept { return enabled.load(std::memory_order_acquire); }
		void Enable() noexcept { enabled.store(true, std::memory_order_release); }
		void Disable() noexcept { enabled.store(false, std::memory_order_release); }

	private:
		std::atomic<bool> enabled{ false };
	};

	// F4SE retains callback pointers without an unregister operation, but unloads
	// a DLL whose Load returns false. A partial or uncertain publication must
	// therefore retain its code even though initialization still reports failure.
	template <class ModuleLease>
	class Registration
	{
	public:
		Registration(Gate& callbackGate, ModuleLease& moduleLease) noexcept :
			gate(callbackGate), lease(moduleLease)
		{
			gate.Disable();
		}

		~Registration() noexcept
		{
			if (!committed && (published || publicationInProgress)) {
				lease.KeepLoaded();
			}
		}

		Registration(const Registration&) = delete;
		Registration& operator=(const Registration&) = delete;

		template <class Publish>
		[[nodiscard]] bool Attempt(Publish&& publish)
		{
			// An exception can occur after the callback was stored. Preserve the
			// lease in that uncertain case, including the first registration call.
			publicationInProgress = true;
			const bool accepted = std::forward<Publish>(publish)();
			publicationInProgress = false;
			published |= accepted;
			return accepted;
		}

		// The caller commits only after every required registration and every
		// potentially throwing initialization step has succeeded.
		void Commit() noexcept
		{
			committed = true;
			gate.Enable();
		}

	private:
		Gate& gate;
		ModuleLease& lease;
		bool published{};
		bool publicationInProgress{};
		bool committed{};
	};
}
