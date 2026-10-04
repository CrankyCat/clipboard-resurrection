// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <cmath>

namespace Clipboard::ImportTransform
{

template <class Point>
[[nodiscard]] bool IsFinite(const Point& point) noexcept
{
	return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

// Keep the historical tool-relative transform, but reject invalid inputs and
// float overflow before a pattern row can create or move an engine reference.
// Outputs remain unchanged on failure; the caller keeps a null physical row.
template <class SourcePosition, class SourceRotation, class Point>
[[nodiscard]] bool Compose(
	const SourcePosition& sourcePosition, const SourceRotation& sourceRotation,
	const Point& localPosition, const Point& localRotation,
	Point& outputPosition, Point& outputRotation) noexcept
{
	if (!IsFinite(sourcePosition) || !IsFinite(sourceRotation) ||
		!IsFinite(localPosition) || !IsFinite(localRotation)) {
		return false;
	}
	const float cosine = std::cos(-sourceRotation.z);
	const float sine = std::sin(-sourceRotation.z);
	const Point position{
		localPosition.x * cosine - localPosition.y * sine + sourcePosition.x,
		localPosition.y * cosine + localPosition.x * sine + sourcePosition.y,
		localPosition.z + sourcePosition.z
	};
	const Point rotation{ localRotation.x, localRotation.y, localRotation.z + sourceRotation.z };
	if (!IsFinite(position) || !IsFinite(rotation)) {
		return false;
	}
	outputPosition = position;
	outputRotation = rotation;
	return true;
}

}
