// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Clipboard::ImportJobs
{
	inline constexpr std::uint32_t kMaximumRows = 262144;
	inline constexpr std::uint32_t kRowsPerSlice = 64;
	inline constexpr std::uint32_t kMaximumPasses = 60;
	inline constexpr std::int32_t kRetryDelayMs = 500;
	inline constexpr std::uint32_t kSliceBudgetMs = 4;

	[[nodiscard]] constexpr bool SliceComplete(std::uint32_t visits, std::uint64_t elapsedMs) noexcept
	{
		return visits >= kRowsPerSlice || (visits != 0 && elapsedMs >= kSliceBudgetMs);
	}

	enum class RowStage : std::uint8_t
	{
		Initial,
		Waiting,
		Complete,
		Missing,
		Failed
	};

	struct Row
	{
		// Actual reference transform captured while disabled. Rotation is radians.
		std::array<float, 6> transform{};
		RowStage stage{ RowStage::Initial };
		bool eligible{};
		bool handled{};
		bool restored{};
		bool generator{};
	};

	struct State
	{
		std::vector<Row> rows;
		std::uint32_t cursor{};
		std::uint32_t passes{};
		std::uint32_t elapsedMs{};
		bool preparing{ true };
		bool cancelled{};
		bool done{};

		[[nodiscard]] bool HasPending() const noexcept
		{
			for (const auto& row : rows) {
				if (row.stage == RowStage::Initial || row.stage == RowStage::Waiting) {
					return true;
				}
			}
			return false;
		}

		// Explicit fields, no C++ padding/pointers/addresses in co-saves. Read and
		// write callbacks are shared with the versioned F4SE functor serializer.
		template <class Write>
		[[nodiscard]] bool Save(Write&& write) const
		{
			if (rows.size() > kMaximumRows) { return false; }
			const auto count = static_cast<std::uint32_t>(rows.size());
			if (!write(count) || !write(cursor) || !write(passes) ||
				!write(elapsedMs) || !write(preparing) || !write(cancelled) || !write(done)) {
				return false;
			}
			for (const auto& row : rows) {
				const auto stage = static_cast<std::uint8_t>(row.stage);
				if (!write(stage) || !write(row.eligible) || !write(row.handled) ||
					!write(row.restored) || !write(row.generator)) {
					return false;
				}
				for (const auto value : row.transform) {
					if (!write(value)) { return false; }
				}
			}
			return true;
		}

		template <class Read>
		[[nodiscard]] bool Load(Read&& read)
		{
			State loaded;
			const auto readBool = [&](bool& value) {
				std::uint8_t byte{};
				if (!read(byte) || byte > 1) { return false; }
				value = byte != 0;
				return true;
			};
			std::uint32_t count{};
			if (!read(count) || count > kMaximumRows || !read(loaded.cursor) || loaded.cursor > count ||
				!read(loaded.passes) || loaded.passes > kMaximumPasses || !read(loaded.elapsedMs) ||
				!readBool(loaded.preparing) || !readBool(loaded.cancelled) || !readBool(loaded.done)) {
				return false;
			}
			loaded.rows.resize(count);
			for (auto& row : loaded.rows) {
				std::uint8_t stage{};
				if (!read(stage) || stage > static_cast<std::uint8_t>(RowStage::Failed) ||
					!readBool(row.eligible) || !readBool(row.handled) || !readBool(row.restored) || !readBool(row.generator)) {
					return false;
				}
				row.stage = static_cast<RowStage>(stage);
				for (auto& value : row.transform) {
					if (!read(value) || !std::isfinite(value)) { return false; }
				}
				if ((row.handled && !row.eligible) || (row.restored && !row.handled)) { return false; }
			}
			*this = std::move(loaded);
			return true;
		}
	};
}
