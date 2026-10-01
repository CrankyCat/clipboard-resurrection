// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include "Localization.h"

namespace Clipboard::Localization
{
	// Load English before registering the native, without reading game settings.
	// GameDataReady subsequently snapshots the same setting used by F4SE's
	// Scaleform translator. Language changes require a game restart, as do ESP
	// string tables and the MCM translation catalog.
	void InitializeRuntime(const std::filesystem::path& catalogDirectory);
	void RefreshRuntimeLanguage();
	[[nodiscard]] std::string GetRuntimeText(std::string_view key, const Arguments& arguments);
}
