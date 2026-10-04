// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#include "ImportCursorPayload.h"

#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
	struct Cursor
	{
		std::uint32_t sourceFormID{};
		std::size_t nextPoint{};
		bool started{}, candidate{}, complete{};
	};

	struct Bytes
	{
		std::vector<std::uint8_t> data;
		std::size_t cursor{};
		std::size_t writeLimit{ std::numeric_limits<std::size_t>::max() };

		template <class T> bool Write(const T& value)
		{
			if (data.size() >= writeLimit) { return false; }
			const auto* first = reinterpret_cast<const std::uint8_t*>(&value);
			data.insert(data.end(), first, first + sizeof(T));
			return true;
		}
		template <class T> bool Read(T& value)
		{
			if (cursor > data.size() || sizeof(T) > data.size() - cursor) { return false; }
			std::memcpy(&value, data.data() + cursor, sizeof(T));
			cursor += sizeof(T);
			return true;
		}
	};

	// Independent representation of the prior functor's bool-by-bool writer.
	Bytes HistoricalPayload(const std::vector<Cursor>& cursors)
	{
		static_assert(sizeof(bool) == 1);
		Bytes result;
		for (const auto& cursor : cursors) {
			(void)result.Write(cursor.started);
			(void)result.Write(cursor.candidate);
		}
		return result;
	}

	template <bool Retain, class Check>
	void CheckPayloads(Check&& check)
	{
		using namespace Clipboard::ImportCursorPayload;
		for (const std::size_t rows : { 0U, 1U, 127U, 128U, 129U, 10000U }) {
			std::vector<Cursor> cursors;
			Resize<Retain>(cursors, rows);
			check(Retain ? cursors.size() == rows : cursors.empty() && cursors.capacity() == 0,
				"only reconnection allocates per-row live cursor storage");
			std::vector<Cursor> historical(rows);
			if constexpr (Retain) {
				for (std::size_t row = 0; row < rows; ++row) {
					// Exercise the three valid states: 00, 10, 11. The other fields
					// are transient and must never appear in the saved byte stream.
					cursors[row] = { static_cast<std::uint32_t>(row + 1), row + 12,
						row % 3 != 0, row % 3 == 2, true };
					historical[row] = cursors[row];
				}
			}
			const auto old = HistoricalPayload(historical);
			Bytes saved;
			check(Save<Retain>(cursors, rows, [&](const auto& value) { return saved.Write(value); }) &&
				saved.data == old.data && saved.data.size() == rows * 2,
				"new writer matches historical two-byte-per-row payload exactly");
			// The real functor has subsequent fields in some variants. A marker
			// detects under/over-consumption without mirroring the helper's loop.
			saved.data.push_back(0xA7);
			std::vector<Cursor> loaded;
			check(Load<Retain>(loaded, rows, [&](auto& value) { return saved.Read(value); }) &&
				saved.cursor == old.data.size() && saved.data[saved.cursor] == 0xA7,
				"loader consumes precisely the historical cursor fields");
			if constexpr (Retain) {
				bool matched = loaded.size() == historical.size();
				for (std::size_t row = 0; row < loaded.size(); ++row) {
					matched &= loaded[row].started == historical[row].started &&
						loaded[row].candidate == historical[row].candidate &&
						loaded[row].sourceFormID == 0 && loaded[row].nextPoint == 0 && !loaded[row].complete;
				}
				check(matched, "reconnect restores counted flags while transient graph scan state restarts");
			} else {
				check(loaded.empty() && loaded.capacity() == 0,
					"non-reconnect reads historical payload without allocating ignored cursors");
			}
			Bytes resaved;
			check(Save<Retain>(loaded, rows, [&](const auto& value) { return resaved.Write(value); }) &&
				resaved.data == old.data, "load/save preserves every historical producer byte");
		}

		// Both operation families retain the old reader's exact flag validation,
		// even where values will be discarded. Test every byte value independently.
		bool validationMatches = true, storageMatches = true;
		for (std::uint16_t first = 0; first <= 255; ++first) {
			for (std::uint16_t second = 0; second <= 255; ++second) {
				Bytes input;
				input.data = { static_cast<std::uint8_t>(first), static_cast<std::uint8_t>(second), 0xA7 };
				std::vector<Cursor> loaded;
				const bool accepted = Load<Retain>(loaded, 1, [&](auto& value) { return input.Read(value); });
				const bool expected = first <= 1 && second <= 1 && (first != 0 || second == 0);
				validationMatches &= accepted == expected && input.cursor == (first > 1 ? 1U : 2U);
				if constexpr (!Retain) {
					storageMatches &= loaded.empty() && loaded.capacity() == 0;
				}
			}
		}
		check(validationMatches, "all 65536 flag-byte pairs retain historical acceptance and failure consumption");
		check(storageMatches, "discarding valid or invalid historical flags allocates no cursor storage");

		std::vector<Cursor> cursors;
		Resize<Retain>(cursors, 3);
		for (std::size_t length = 0; length < 6; ++length) {
			Bytes truncated;
			truncated.data.resize(length, 0);
			std::vector<Cursor> loaded;
			check(!Load<Retain>(loaded, 3, [&](auto& value) { return truncated.Read(value); }) &&
				truncated.cursor == length, "every truncation fails without reading past available bytes");
			Bytes failedWrite;
			failedWrite.writeLimit = length;
			check(!Save<Retain>(cursors, 3, [&](const auto& value) { return failedWrite.Write(value); }) &&
				failedWrite.data.size() == length, "every failed write propagates at the exact byte boundary");
		}
	}
}

int main()
{
	std::size_t checks{}, failures{};
	const auto check = [&](bool success, const char* message) {
		++checks;
		if (!success) { ++failures; std::cerr << message << '\n'; }
	};
	CheckPayloads<false>(check);
	CheckPayloads<true>(check);
	std::vector<Cursor> mismatch(2);
	Bytes untouched;
	check(!Clipboard::ImportCursorPayload::Save<true>(mismatch, 3,
		[&](const auto& value) { return untouched.Write(value); }) && untouched.data.empty(),
		"reconnect row/storage mismatch is rejected before writing");
	std::cout << checks << " import cursor payload checks, " << failures
		<< " failures; non-reconnect 10000-row cursor allocation count 10000 -> 0\n";
	return failures ? 1 : 0;
}
