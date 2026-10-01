// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Clipboard::ImportCursorPayload
{
	// Every historical import functor writes started/candidate for each physical
	// row. Preparation/generator jobs never use those flags, but their two bytes
	// must remain in the payload even when no live conduit cursors are allocated.
	template <bool Retain, class Cursor>
	void Resize(std::vector<Cursor>& cursors, std::size_t rows)
	{
		if constexpr (Retain) { cursors.resize(rows); }
		else { (void)rows; cursors.clear(); }
	}

	template <bool Retain, class Cursor, class Write>
	[[nodiscard]] bool Save(const std::vector<Cursor>& cursors, std::size_t rows, Write&& write)
	{
		if constexpr (Retain) {
			if (cursors.size() != rows) { return false; }
		} else { (void)cursors; }
		for (std::size_t row = 0; row < rows; ++row) {
			std::uint8_t started{}, candidate{};
			if constexpr (Retain) {
				started = static_cast<std::uint8_t>(cursors[row].started);
				candidate = static_cast<std::uint8_t>(cursors[row].candidate);
			}
			if (!write(started) || !write(candidate)) { return false; }
		}
		return true;
	}

	template <bool Retain, class Cursor, class Read>
	[[nodiscard]] bool Load(std::vector<Cursor>& cursors, std::size_t rows, Read&& read)
	{
		Resize<Retain>(cursors, rows);
		for (std::size_t row = 0; row < rows; ++row) {
			std::uint8_t started{}, candidate{};
			if (!read(started) || started > 1 || !read(candidate) || candidate > 1 ||
				(candidate && !started)) { return false; }
			if constexpr (Retain) {
				cursors[row].started = started != 0;
				cursors[row].candidate = candidate != 0;
			}
		}
		return true;
	}
}
