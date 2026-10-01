// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Clipboard::OptionalFiltering
{
	// Read-only representation of the keyword array at ExtraLinkedRef + 0x18.
	// OG/NG/AE getter disassembly agrees on this layout. The second entry word
	// is deliberately opaque: only the engine resolves its pointer/FormID modes.
	// See Docs/MANUAL_SELECTION_STUTTER.md and its four-fixture evidence.
	struct LinkedRefKeywordArray
	{
		std::uint32_t capacityAndLocal;
		std::uint32_t padding04;
		std::array<std::uintptr_t, 2> storage;
		std::uint32_t size;
		std::uint32_t padding1C;
	};
	static_assert(sizeof(std::uintptr_t) == 8);
	static_assert(sizeof(LinkedRefKeywordArray) == 0x20);
	static_assert(offsetof(LinkedRefKeywordArray, storage) == 0x08);
	static_assert(offsetof(LinkedRefKeywordArray, size) == 0x18);

	template <class IsConfiguredKeyword>
	[[nodiscard]] bool MayContainConfiguredChildSlot(
		const void* arrayMemory, bool hasAliasLinks, IsConfiguredKeyword isConfiguredKeyword)
	{
		// AliasInstanceArray is consulted before ExtraLinkedRef by the engine.
		// Unknown metadata must also fall back to the existing engine queries.
		if (hasAliasLinks || !arrayMemory) {
			return true;
		}
		LinkedRefKeywordArray array{};
		std::memcpy(&array, arrayMemory, sizeof(array));
		const bool local = (array.capacityAndLocal & 0x80000000u) != 0;
		const auto capacity = array.capacityAndLocal & 0x7FFFFFFFu;
		if (array.size > capacity || array.size > 65536 || (local && array.size > 1)) {
			return true;
		}
		if (array.size == 0) {
			return false;
		}
		const auto* entries = local ? reinterpret_cast<const std::byte*>(array.storage.data()) :
			reinterpret_cast<const std::byte*>(array.storage[0]);
		if (!entries) {
			return true;
		}
		for (std::uint32_t i = 0; i < array.size; ++i) {
			std::uintptr_t keyword{};
			std::memcpy(&keyword, entries + static_cast<std::size_t>(i) * 0x10, sizeof(keyword));
			if (isConfiguredKeyword(keyword)) {
				return true;
			}
		}
		return false;
	}
}
