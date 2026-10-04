// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#include "PCH.h"
#include "LoggingPolicy.h"
#include "LocalizationRuntime.h"

#include <RE/Bethesda/Settings.h>

namespace Clipboard::Localization
{
	namespace
	{
		struct RuntimeCatalogs
		{
			std::mutex mutex;
			std::filesystem::path directory;
			Catalog english;
			Catalog translated;
			std::set<std::string, std::less<>> missingKeys;
		};

		RuntimeCatalogs& RuntimeState()
		{
			static RuntimeCatalogs state;
			return state;
		}

		RE::Setting* FindLanguageSetting(RE::INISettingCollection* collection)
		{
			if (!collection) {
				return nullptr;
			}
			// F4SE GameSettings.h/.cpp in 0.6.23, 0.7.2 and 0.7.9 starts
			// with Node* at 0x120 and treats 0x118 as unused. CommonLib maps
			// 0x118/0x120 to the embedded BSSimpleList node, so advance past
			// that sentinel WITHOUT dereferencing it as a Setting. Its stock
			// GetSetting instead dereferences the embedded value unguarded.
			// Settings are read only after GameDataReady, never modified here.
			auto entry = collection->settings.begin();
			const auto end = collection->settings.end();
			if (entry != end) {
				++entry;
			}
			for (; entry != end; ++entry) {
				if (auto* setting = *entry; setting && setting->GetKey() == "sLanguage:General") {
					return setting;
				}
			}
			return nullptr;
		}

		std::string ReadGameLanguage()
		{
			static_assert(sizeof(RE::Setting) == 0x18);
			static_assert(sizeof(RE::INISettingCollection) == 0x128);
			static_assert(sizeof(RE::INIPrefSettingCollection) == 0x128);
			auto* setting = FindLanguageSetting(RE::INISettingCollection::GetSingleton());
			if (!setting) {
				setting = FindLanguageSetting(RE::INIPrefSettingCollection::GetSingleton());
			}
			if (!setting || setting->GetType() != RE::Setting::SETTING_TYPE::kString) {
				logger::warn("Clipboard localization could not read sLanguage:General; using English");
				return "en";
			}
			return std::string(setting->GetString());
		}

		void LogRejectedCatalog(std::string_view language, const ParseResult& result)
		{
			logger::error("Clipboard {} localization catalog rejected at line {}: {}",
				language, result.line, result.error);
		}
	}

	void InitializeRuntime(const std::filesystem::path& catalogDirectory)
	{
		auto& state = RuntimeState();
		const std::lock_guard lock(state.mutex);
		state.directory = catalogDirectory;
		state.english.clear();
		state.translated.clear();
		state.missingKeys.clear();
		auto english = ReadCatalog(state.directory / "en.tsv");
		if (english) {
			state.english = std::move(english.entries);
			CLIPBOARD_DEBUG_LOG(logger::info("Clipboard English localization loaded: {} entries", state.english.size()));
		} else {
			LogRejectedCatalog("English", english);
		}
	}

	void RefreshRuntimeLanguage()
	{
		const auto gameLanguage = ReadGameLanguage();
		const auto language = SelectLanguage(gameLanguage);
		auto& state = RuntimeState();
		const std::lock_guard lock(state.mutex);
		state.translated.clear();
		state.missingKeys.clear();
		if (language != "en") {
			// SelectLanguage returns a fixed allowlisted suffix. The raw setting
			// is never accepted as a filename or path component.
			auto translated = ReadCatalog(state.directory / (std::string(language) + ".tsv"));
			if (translated) {
				std::size_t mismatches = 0;
				for (auto& [key, value] : translated.entries) {
					const auto source = state.english.find(key);
					PlaceholderCounts sourceCounts;
					PlaceholderCounts localizedCounts;
					if (source == state.english.end() ||
						!GetPlaceholderCounts(source->second, sourceCounts) ||
						!GetPlaceholderCounts(value, localizedCounts) || sourceCounts != localizedCounts) {
						++mismatches;
						continue;
					}
					state.translated.emplace(key, std::move(value));
				}
				if (mismatches != 0) {
					logger::warn("Clipboard rejected {} {} keys without matching English keys/placeholders", mismatches, language);
				}
			} else {
				LogRejectedCatalog(language, translated);
			}
		}
		CLIPBOARD_DEBUG_LOG(logger::info("Clipboard localization selected {} from sLanguage:General='{}'; {} English and {} translated entries",
			language, gameLanguage, state.english.size(), state.translated.size()));
	}

	std::string GetRuntimeText(std::string_view key, const Arguments& arguments)
	{
		auto& state = RuntimeState();
		const std::lock_guard lock(state.mutex);
		// Do not consume the once-per-key diagnostic while logging is disabled;
		// the same missing key must still be reportable after logging is enabled.
		if (!state.english.contains(key) && state.missingKeys.size() < kMaxEntries &&
			state.missingKeys.emplace(key.substr(0, kMaxKeyBytes)).second) {
			logger::error("Clipboard localization key unavailable: {}", key.substr(0, kMaxKeyBytes));
		}
		return Resolve(state.english, state.translated, key, arguments);
	}
}
