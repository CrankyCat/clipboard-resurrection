// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <cstdint>
#include <optional>

namespace RE
{
	class TESObjectREFR;
}

namespace Clipboard::ImportAnimation
{
	inline constexpr std::int32_t kUnavailable = -1;
	inline constexpr std::int32_t kWorkshopPowerSequences = 1;
	inline constexpr std::int32_t kGenericMotor = 2;

	// Inspect existing bound scripts, including derived GenericMotorScript
	// instances. No type loading, script creation, binding or dispatch occurs.
	// An unavailable inspection returns nullopt so preparation can wait/retry
	// before deciding whether keyframing is appropriate for this reference.
	[[nodiscard]] std::optional<bool> HasGenericMotor(RE::TESObjectREFR* reference) noexcept;

	// Null/actor rows have no work. Invalid or unloaded non-actor references
	// and failed inspection return -1. Otherwise return the capability bitset
	// above, independently of generator, sound and activator classification.
	// Model sequence names do not guarantee that a graph accepts an event.
	[[nodiscard]] std::int32_t GetKind(RE::TESObjectREFR* reference) noexcept;
}
