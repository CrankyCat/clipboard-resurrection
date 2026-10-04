#pragma once

#include "LegacyVMArray.h"

#include <cstdint>

namespace Clipboard::PatternRows
{
	enum class WireEndpointStatus
	{
		kReady,
		kFirstIndexOutOfRange,
		kSecondIndexOutOfRange,
		kFirstRowMissing,
		kSecondRowMissing
	};

	template <class T>
	[[nodiscard]] WireEndpointStatus ResolveWireEndpoints(
		const VMArray<T*>& a_rows,
		std::uint32_t a_firstIndex,
		std::uint32_t a_secondIndex,
		T*& a_first,
		T*& a_second) noexcept
	{
		a_first = nullptr;
		a_second = nullptr;
		if (a_firstIndex >= a_rows.Length()) {
			return WireEndpointStatus::kFirstIndexOutOfRange;
		}
		if (a_secondIndex >= a_rows.Length()) {
			return WireEndpointStatus::kSecondIndexOutOfRange;
		}
		if (!a_rows.Get(&a_first, a_firstIndex) || !a_first) {
			return WireEndpointStatus::kFirstRowMissing;
		}
		if (!a_rows.Get(&a_second, a_secondIndex) || !a_second) {
			return WireEndpointStatus::kSecondRowMissing;
		}
		return WireEndpointStatus::kReady;
	}
}
