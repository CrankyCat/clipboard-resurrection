// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <cmath>
#include <cstddef>

namespace Clipboard::SelectionGeometry
{

struct Point
{
	float x{};
	float y{};
	float z{};
};

struct Geometry
{
	Point minimum{};
	Point maximum{};
	float minimumScale{ 1.0F };
	float maximumScale{ 1.0F };
	float averageScale{ 1.0F };

	[[nodiscard]] Point Area() const noexcept
	{
		return { maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z };
	}

	[[nodiscard]] Point Center() const noexcept
	{
		// Preserve the old float addition followed by double division and float
		// conversion, including its rounding/overflow behavior.
		return {
			static_cast<float>((minimum.x + maximum.x) / 2.0),
			static_cast<float>((minimum.y + maximum.y) / 2.0),
			static_cast<float>((minimum.z + maximum.z) / 2.0)
		};
	}
};

inline void IncludeAxis(float value, float& minimum, float& maximum) noexcept
{
	if (value < minimum) {
		minimum = value;
	}
	else if (value > maximum) {
		maximum = value;
	}
}

// The linked-selection snapshot contains a valid first reference when nonempty.
// Later null rows retain the historical skip and whole-snapshot average divisor.
// Nothing here inspects wires, base records, plugins, or workshop membership.
// A center-only query does not read scales or accumulate unused totals.
template <bool IncludeScales = true, class References, class ReadPosition, class ReadScale>
[[nodiscard]] Geometry Measure(const References& references,
	ReadPosition&& readPosition, ReadScale&& readScale)
{
	Geometry geometry;
	if (references.empty()) {
		return geometry;
	}
	geometry.minimum = readPosition(references[0]);
	geometry.maximum = geometry.minimum;
	float totalScale = 0.0F;
	if constexpr (IncludeScales) {
		geometry.minimumScale = readScale(references[0]);
		geometry.maximumScale = geometry.minimumScale;
		totalScale = geometry.minimumScale;
	}
	for (std::size_t index = 1; index < references.size(); ++index) {
		if (const auto reference = references[index]) {
			float scale = 0.0F;
			if constexpr (IncludeScales) {
				scale = readScale(reference);
			}
			const auto position = readPosition(reference);
			IncludeAxis(position.x, geometry.minimum.x, geometry.maximum.x);
			IncludeAxis(position.y, geometry.minimum.y, geometry.maximum.y);
			IncludeAxis(position.z, geometry.minimum.z, geometry.maximum.z);
			if constexpr (IncludeScales) {
				IncludeAxis(scale, geometry.minimumScale, geometry.maximumScale);
				totalScale += scale;
			}
		}
	}
	if constexpr (IncludeScales) {
		geometry.averageScale = totalScale / static_cast<float>(references.size());
	}
	return geometry;
}

// Relative changes retain the existing lower-then-upper clamp order.
// Restore is an absolute operation handled separately by PlanScale below.
[[nodiscard]] inline float ScaleFactor(const Geometry& geometry, float requested) noexcept
{
	float factor = requested;
	if (!std::isfinite(factor) || factor <= 0.0F) {
		factor = 1.0F;
	}
	if (geometry.minimumScale > 0.0F && geometry.minimumScale * factor < 0.01F) {
		factor = 0.01F / geometry.minimumScale;
	}
	if (geometry.maximumScale > 0.0F && geometry.maximumScale * factor > 10.0F) {
		factor = 10.0F / geometry.maximumScale;
	}
	return factor;
}

struct ScalePlan
{
	bool restore{};
	bool movePositions{};
	float factor{ 1.0F };

	[[nodiscard]] float ObjectScale(float current) const noexcept
	{
		// Assign exactly 100%; do not multiply by an approximate reciprocal.
		return restore ? 1.0F : current * factor;
	}
};

[[nodiscard]] inline ScalePlan PlanScale(const Geometry& geometry, float requested, bool whole) noexcept
{
	if (requested <= 0.0F) {
		// A uniform group has a single reversible spacing factor. Mixed scales
		// do not: restore every object without moving their positions.
		if (whole && geometry.minimumScale == geometry.maximumScale &&
			std::isfinite(geometry.minimumScale) && geometry.minimumScale > 0.0F) {
			const float factor = 1.0F / geometry.minimumScale;
			if (std::isfinite(factor) && factor > 0.0F) {
				return { true, factor != 1.0F, factor };
			}
		}
		return { true, false, 1.0F };
	}
	return { false, whole, ScaleFactor(geometry, requested) };
}

}
