// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <cstdint>
#include <type_traits>

namespace Clipboard::SerializationValues
{

// The historical payload stores a one-byte C++ bool. Decode through byte
// storage so a malformed co-save cannot create an invalid bool representation.
// Only successful canonical input may replace the caller's destination.
template <class Read>
[[nodiscard]] bool ReadBool(Read&& read, bool& value)
{
	std::uint8_t byte{};
	if (!read(byte) || byte > 1) {
		return false;
	}
	value = byte != 0;
	return true;
}

}
