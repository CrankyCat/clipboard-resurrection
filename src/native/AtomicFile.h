// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace Clipboard::AtomicFile
{
	enum class Stage { Create, Write, Flush, Verify, Close, Replace, Complete };
	struct Result
	{
		Stage stage{ Stage::Create };
		std::uint32_t error{};
		explicit operator bool() const noexcept { return stage == Stage::Complete; }
	};

	// The transaction owns only its temporary file until Replace succeeds.
	// Keeping the sequence separate allows deterministic failure checks without
	// exposing a diagnostic failure-injection setting in the game.
	template <class Transaction>
	Result Commit(Transaction& file, std::string_view contents)
	{
		if (!file.Create()) { return { Stage::Create, file.Error() }; }
		if (!file.Write(contents)) { return { Stage::Write, file.Error() }; }
		if (!file.Flush()) { return { Stage::Flush, file.Error() }; }
		if (!file.Verify(contents)) { return { Stage::Verify, file.Error() }; }
		if (!file.Close()) { return { Stage::Close, file.Error() }; }
		if (!file.Replace()) { return { Stage::Replace, file.Error() }; }
		return { Stage::Complete, 0 };
	}

	// Same-directory Windows rename: no delete/truncate of the destination, no
	// cross-volume copy fallback. Failure leaves an existing destination intact.
	Result Write(const std::filesystem::path& destination, std::string_view contents);
}
