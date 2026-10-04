// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include "LocalizationRuntime.h"
#include "ImportPercentage.h"
#include "ImportProgressHud.h"
#include "WorkshopProgress.h"
#include <RE/Bethesda/SendHUDMessage.h>
#include <memory>

namespace Clipboard::ImportProgress
{
	// Each phase has a generation. Queued UI work reads the shared latest counts;
	// old phases can neither overwrite nor clear a newer phase's single HUD line.
	class PercentageNotice
	{
	public:
		PercentageNotice() = default;
		~PercentageNotice() { Stop(); }
		PercentageNotice(const PercentageNotice&) = delete;
		PercentageNotice& operator=(const PercentageNotice&) = delete;
		void Start(std::uint32_t owner, Phase phase)
		{
			Stop();
			_generation = g_displayChannel.Begin(owner, phase);
			QueuePercentageRefresh();
		}
		void Update(std::uint32_t done, std::uint32_t total) noexcept
		{
			if (!_generation) { return; }
			g_displayChannel.Update(_generation, done, total);
			QueuePercentageRefresh();
		}
		void Finish() noexcept
		{
			if (!_generation) { return; }
			g_displayChannel.Finish(_generation);
			_generation = 0; // terminal display survives this small owner's lifetime
			QueuePercentageRefresh();
		}
		void Stop() noexcept
		{
			if (!_generation) { return; }
			g_displayChannel.Stop(_generation);
			_generation = 0;
			QueuePercentageRefresh();
		}
	private:
		std::uint64_t _generation{};
	};

	// Only the two workshop-level calls contribute units. Each eligible object
	// contributes one placed half and one moved half; object-only handlers do not
	// enter the denominator. The actual callback publishes immediately, even when
	// the native admission functor is waiting for its next scheduler slice.
	class RegistrationProgress
	{
	public:
		explicit RegistrationProgress(std::size_t count) : units(count) {}
		void Start(std::uint32_t owner) { std::scoped_lock lock(mutex); notice.Start(owner, Phase::Workshop); RefreshLocked(); }
		void Require(std::size_t row, std::uint8_t bits) { std::scoped_lock lock(mutex); if (active) { units.Require(row, bits); RefreshLocked(); } }
		void Omit(std::size_t row, std::uint8_t bits) { std::scoped_lock lock(mutex); if (active) { units.Omit(row, bits); RefreshLocked(); } }
		void Returned(std::size_t row, std::uint8_t bits) noexcept
		{
			std::scoped_lock lock(mutex);
			if (active) { units.Complete(row, bits); RefreshLocked(); }
		}
		void Refresh() noexcept { std::scoped_lock lock(mutex); if (active) { RefreshLocked(); } }
		void Finish(bool success) noexcept
		{
			std::scoped_lock lock(mutex);
			if (!active) { return; }
			active = false;
			if (success && units.AllDone()) { notice.Finish(); } else { notice.Stop(); }
		}
		std::pair<std::uint32_t, std::uint32_t> Counts() { std::scoped_lock lock(mutex); return { units.Done(), units.Total() }; }
	private:
		void RefreshLocked() noexcept { notice.Update(units.Done(), units.Total()); }
		std::mutex mutex;
		RowUnits units;
		PercentageNotice notice;
		bool active{ true };
	};

	// UI-only, process-local state. It owns no import references and is never
	// serialized. Queued tasks retain the small display gate, not the native job.
	class Notice
	{
	public:
		Notice() = default;
		~Notice() { Stop(); }
		Notice(const Notice&) = delete;
		Notice& operator=(const Notice&) = delete;

		void Start(bool enabled) noexcept
		{
			_enabled = enabled;
			// A short phase needs no reminder. Start at ten seconds and do not
			// emit a second immediate message when placement enters preparation.
			if (enabled) { (void)_pulse.Due(NowMs(), false); }
		}
		template <class IsActive>
		void Update(IsActive isActive) noexcept
		{
			if (!_enabled || !_pulse.Due(NowMs(), false)) { return; }
			try {
				if (!isActive()) { Stop(); return; }
				const auto* tasks = F4SE::GetTaskInterface();
				if (!tasks || tasks->Version() < F4SE::TaskInterface::kVersion) { return; }
				if (!_notice) { _notice = std::make_shared<WorkshopCallbacks::ProgressNotice>(); }
				if (!_notice->TryQueue()) { return; }
				tasks->AddTask([notice = _notice, isActive]() noexcept {
					try {
						if (isActive()) {
							const auto text = Localization::GetRuntimeText("$Clipboard_ImportingObjects", {});
							notice->DisplayIfActive([&] {
								if (isActive()) { RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, false, true); }
							});
						}
					} catch (...) {
						try { F4SE::log::warn("Import objects notice could not be displayed"); } catch (...) {}
					}
					notice->Complete();
				});
			} catch (...) {
				if (_notice) { _notice->Complete(); }
				try { F4SE::log::warn("Import objects notice could not be queued"); } catch (...) {}
			}
		}
		void Update() noexcept { Update([] { return true; }); }
		void Stop() noexcept
		{
			_enabled = false;
			if (_notice) { _notice->Stop(); }
		}

	private:
		static std::uint64_t NowMs() noexcept { return WorkshopCallbacks::ClockNs() / 1000000; }
		bool _enabled{};
		WorkshopCallbacks::ProgressPulse _pulse;
		std::shared_ptr<WorkshopCallbacks::ProgressNotice> _notice;
	};
}
