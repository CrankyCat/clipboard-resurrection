#pragma once

#include <cmath>
#include <cstddef>

namespace Clipboard::WideSelection
{
	struct Position
	{
		double x;
		double y;
		double z;
	};

	// Comparing positions across interiors/worldspaces is not meaningful.
	// Persistent exterior storage is still in the same coordinate space.
	[[nodiscard]] inline double Distance(bool a_sameSpace, Position a_origin, Position a_position)
	{
		if (!a_sameSpace || !std::isfinite(a_origin.x) || !std::isfinite(a_origin.y) ||
			!std::isfinite(a_origin.z) || !std::isfinite(a_position.x) ||
			!std::isfinite(a_position.y) || !std::isfinite(a_position.z)) {
			return -1.0;
		}
		// Engine positions are floats. Double intermediates safely hold their
		// squared deltas without introducing the three-argument hypot runtime DLL.
		const double dx = a_position.x - a_origin.x;
		const double dy = a_position.y - a_origin.y;
		const double dz = a_position.z - a_origin.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	struct Counts
	{
		std::size_t input{};
		std::size_t retained{};
		std::size_t outOfRange{};
		std::size_t invalidDistance{};
		std::size_t persistentCandidates{};
		std::size_t uncappedRetained{};
	};

	[[nodiscard]] inline bool UsesPersistentCellLimit(bool a_isInterior, const void* a_parentCell, const void* a_persistentCell)
	{
		return !a_isInterior && a_parentCell && a_parentCell == a_persistentCell;
	}

	// Do not rebuild this result with Papyrus Array.Add: that operation stops
	// at 128 even when the incoming native array contains many more references.
	// Keep order and instances intact; policy and deduplication are separate.
	template <class Result, class Range, class GetDistance, class RequiresLimit>
	[[nodiscard]] Result Filter(const Range& a_rows, double a_maximum, GetDistance a_getDistance, RequiresLimit a_requiresLimit, Counts& a_counts)
	{
		Result result;
		a_counts = {};
		const bool validMaximum = std::isfinite(a_maximum) && a_maximum >= 0.0;
		for (const auto& row : a_rows) {
			++a_counts.input;
			const bool limited = a_requiresLimit(row);
			a_counts.persistentCandidates += limited ? 1 : 0;
			const double distance = a_getDistance(row);
			if (!std::isfinite(distance) || distance < 0.0 || (limited && !validMaximum)) {
				++a_counts.invalidDistance;
			} else if (limited && distance > a_maximum) {
				++a_counts.outOfRange;
			} else {
				result.push_back(row);
				++a_counts.retained;
				a_counts.uncappedRetained += limited ? 0 : 1;
			}
		}
		return result;
	}

	// The all-limited overload retains the original pure-distance regression
	// fixture. Runtime callers explicitly identify persistent storage below.
	template <class Result, class Range, class GetDistance>
	[[nodiscard]] Result Filter(const Range& a_rows, double a_maximum, GetDistance a_getDistance, Counts& a_counts)
	{
		return Filter<Result>(a_rows, a_maximum, a_getDistance, [](const auto&) { return true; }, a_counts);
	}
}
