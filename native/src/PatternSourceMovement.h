// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <cmath>
#include <cstdint>

namespace Clipboard::SourceMovement
{

struct Space
{
	std::uint32_t cell;
	bool interior;
	std::uint32_t world;
};

// A persistent exterior cell is storage for its worldspace, not a settlement
// boundary. The engine's workshop volumes must approve each actual position.
template <class BoundaryCheck>
bool IsWithinWorkshop(Space positionSpace, Space workshopSpace, float x, float y, float z, BoundaryCheck&& boundaryCheck)
{
	if (!positionSpace.cell || !workshopSpace.cell ||
		!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
		std::abs(x) > 100000000.0F || std::abs(y) > 100000000.0F || std::abs(z) > 100000000.0F) {
		return false;
	}
	if (positionSpace.interior || workshopSpace.interior) {
		if (positionSpace.interior != workshopSpace.interior || positionSpace.cell != workshopSpace.cell) {
			return false;
		}
	} else if (!workshopSpace.world || positionSpace.world != workshopSpace.world) {
		return false;
	}
	return boundaryCheck(x, y, z);
}

}
