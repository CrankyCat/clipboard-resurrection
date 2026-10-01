// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <utility>

namespace Clipboard::ImportDuplicates
{
	// Allow only export/composition rounding, not visibly overlapping pieces.
	inline constexpr double kPositionTolerance = 0.1;
	inline constexpr double kAngleTolerance = std::numbers::pi / 18000.0; // 0.01 degree
	inline constexpr double kScaleTolerance = 0.0001;
	struct Transform
	{
		std::array<double, 3> position{};
		std::array<double, 3> angle{};
		double scale{ 1.0 };
	};
	inline bool Finite(const Transform& value)
	{
		for (std::size_t i = 0; i < 3; ++i) {
			if (!std::isfinite(value.position[i]) || !std::isfinite(value.angle[i])) { return false; }
		}
		return std::isfinite(value.scale) && value.scale > 0;
	}
	inline bool Matches(const Transform& left, const Transform& right)
	{
		if (!Finite(left) || !Finite(right) || std::abs(left.scale - right.scale) > kScaleTolerance) { return false; }
		for (std::size_t i = 0; i < 3; ++i) {
			if (std::abs(left.position[i] - right.position[i]) > kPositionTolerance ||
				std::abs(std::remainder(left.angle[i] - right.angle[i], 2.0 * std::numbers::pi)) > kAngleTolerance) {
				return false;
			}
		}
		return true;
	}

	// Search only the same base and narrow X range; avoid a full world scan for
	// every row and quantized-coordinate overflow at large finite coordinates.
	// Payload owns the reference. The caller revalidates its live state before use.
	template <class Payload>
	class Index
	{
	public:
		void Add(std::uint32_t base, const Transform& transform, Payload payload)
		{
			if (base && Finite(transform)) { _entries.emplace(Key{ base, transform.position[0] }, Entry{ transform, std::move(payload) }); }
		}
		template <class Validate>
		const Payload* Find(std::uint32_t base, const Transform& target, Validate&& validate) const
		{
			if (!base || !Finite(target)) { return nullptr; }
			for (auto it = _entries.lower_bound(Key{ base, target.position[0] - kPositionTolerance });
				it != _entries.end() && it->first.first == base && it->first.second <= target.position[0] + kPositionTolerance; ++it) {
				if (Matches(it->second.transform, target) && validate(it->second.payload)) { return &it->second.payload; }
			}
			return nullptr;
		}
	private:
		using Key = std::pair<std::uint32_t, double>;
		struct Entry { Transform transform; Payload payload; };
		std::multimap<Key, Entry> _entries;
	};
}
