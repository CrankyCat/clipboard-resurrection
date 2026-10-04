// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <cstdint>
#include <limits>
#include <unordered_map>

namespace Clipboard::Selection
{
	// One operation owns this index. Row numbers refer to the original physical
	// array: null rows consume a position and repeated FormIDs keep their first
	// match, exactly as the old linear search did. No reference is retained here.
	class FirstReferenceRows
	{
	public:
		explicit FirstReferenceRows(std::uint32_t expectedRows = 0)
		{
			rows_.reserve(expectedRows);
		}

		// Append callers insert only after admission and before appending their
		// new row; a repeated identity neither replaces nor moves the first row.
		bool Insert(std::uint32_t formID, std::uint32_t row)
		{
			return rows_.try_emplace(formID, static_cast<int>(row)).second;
		}

		[[nodiscard]] int Find(std::uint64_t formID) const
		{
			if (formID > std::numeric_limits<std::uint32_t>::max()) {
				return -1;
			}
			const auto found = rows_.find(static_cast<std::uint32_t>(formID));
			return found == rows_.end() ? -1 : found->second;
		}

	private:
		std::unordered_map<std::uint32_t, int> rows_;
	};

	// identityAt returns an optional FormID. None differs from a real reference
	// whose FormID is zero, so this helper also preserves raw lookup semantics
	// outside selection admission (which separately rejects zero identities).
	template <class IdentityAt>
	[[nodiscard]] FirstReferenceRows BuildFirstReferenceRows(
		std::uint32_t rowCount, IdentityAt&& identityAt)
	{
		FirstReferenceRows result(rowCount);
		for (std::uint32_t row = 0; row < rowCount; ++row) {
			if (const auto identity = identityAt(row)) {
				result.Insert(*identity, row);
			}
		}
		return result;
	}
}
