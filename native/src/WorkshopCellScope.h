#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>

namespace Clipboard::WorkshopScope
{

enum class Membership
{
	kOutside,
	kLocation,
	kOwnerFallback
};

// Zero means unavailable, not a matching location. Ownership may fill missing
// location evidence, but must never override a known different settlement.
inline Membership ResolveMembership(
	bool sameSpace,
	std::uint32_t targetLocation,
	std::uint32_t physicalCellLocation,
	std::uint32_t explicitReferenceLocation,
	bool suppliedChildWithMatchingOwner)
{
	if (!sameSpace) {
		return Membership::kOutside;
	}
	const auto knownLocation = physicalCellLocation ? physicalCellLocation : explicitReferenceLocation;
	if (knownLocation) {
		return targetLocation && knownLocation == targetLocation ?
			Membership::kLocation : Membership::kOutside;
	}
	return suppliedChildWithMatchingOwner ? Membership::kOwnerFallback : Membership::kOutside;
}

// Exterior cell keys use signed 16-bit X/Y coordinates, X in the high word.
// floor is essential for positions just west/south of the world origin.
inline std::optional<std::int32_t> ExteriorCellKey(float x, float y)
{
	if (!std::isfinite(x) || !std::isfinite(y)) {
		return std::nullopt;
	}
	const double cellX = std::floor(static_cast<double>(x) / 4096.0);
	const double cellY = std::floor(static_cast<double>(y) / 4096.0);
	if (cellX < -32768.0 || cellX > 32767.0 || cellY < -32768.0 || cellY > 32767.0) {
		return std::nullopt;
	}
	const auto packed = (static_cast<std::uint32_t>(static_cast<std::int32_t>(cellX)) & 0xFFFFu) << 16 |
		(static_cast<std::uint32_t>(static_cast<std::int32_t>(cellY)) & 0xFFFFu);
	return std::bit_cast<std::int32_t>(packed);
}

}
