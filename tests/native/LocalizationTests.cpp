// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#include "Localization.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
	using namespace Clipboard::Localization;
	int checks = 0;
	int failures = 0;
	const auto check = [&](bool passed, const char* description) {
		++checks;
		if (!passed) {
			++failures;
			std::cerr << description << '\n';
		}
	};

	const auto english = ParseCatalog(
		"# Clipboard catalog\r\n\r\n"
		"$Clipboard_Imported\tImported {0} objects and {1} wires in {2} seconds.\r\n"
		"$Clipboard_Empty\tEmpty slot\n"
		"$Clipboard_Path\tC:\\\\Patterns\\nName:\\t{0}\\r\\n{{Example}}\n"
		"$Clipboard_Repeated\t{0} / {0}\n");
	const auto russian = ParseCatalog(
		"\xEF\xBB\xBF$Clipboard_Imported\tЗа {2} сек. импортировано объектов: {0}, проводов: {1}.\n"
		"$Clipboard_Empty\tПустой слот\n"
		"$Clipboard_Repeated\t{0} / {1}\n");
	check(english && english.entries.size() == 4, "CRLF/LF English catalog and comment handling");
	check(russian && russian.entries.size() == 3, "UTF-8 Cyrillic with optional BOM");
	check(english.entries.at("$Clipboard_Path") == "C:\\Patterns\nName:\t{0}\r\n{{Example}}", "all supported TSV escapes decode");
	const Arguments arguments{ "1194", "25", "83.531", "", "", "" };
	check(Resolve(english.entries, russian.entries, "$Clipboard_Imported", arguments) ==
		"За 83.531 сек. импортировано объектов: 1194, проводов: 25.", "Russian whole sentence can reorder arguments");
	check(Resolve(english.entries, {}, "$Clipboard_Imported", arguments) ==
		"Imported 1194 objects and 25 wires in 83.531 seconds.", "unavailable locale falls back to English");
	check(Resolve(english.entries, russian.entries, "$Clipboard_Path", {}) ==
		"C:\\Patterns\nName:\t\r\n{Example}", "missing translated key uses decoded English and literal braces");
	check(Resolve(english.entries, russian.entries, "$Clipboard_Repeated", arguments) == "1194 / 1194",
		"translation with changed placeholder multiset falls back to English");
	check(Resolve(english.entries, russian.entries, "$Clipboard_Unknown", {}) == kUnavailableText,
		"missing English key uses readable emergency fallback");
	check(Resolve({}, russian.entries, "$Clipboard_Empty", {}) == kUnavailableText,
		"translated key cannot replace missing English source");
	check(Resolve(english.entries, russian.entries, "../../ru.tsv", {}) == kUnavailableText,
		"unknown key remains lookup data and never selects a file");

	check(Format("Name: {0}; count: {1}", { "Иван {1} \\n <font color='#fff'>", "7", "", "", "", "" }) ==
		"Name: Иван {1} \\n <font color='#fff'>; count: 7", "user names, markup, braces and backslashes are inserted once");
	check(Format("{5} {4} {3} {2} {1} {0}", { "a", "b", "c", "d", "e", "f" }) == "f e d c b a",
		"all six argument indices are available");
	check(Format("{{{0}}}", { "{1}", "not substituted", "", "", "", "" }) == "{{1}}",
		"literal braces around argument do not cause recursive formatting");
	check(SelectLanguage("ru") == "ru" && SelectLanguage(" RUSSIAN\r\n") == "ru", "Russian locale normalization");
	check(SelectLanguage("en") == "en" && SelectLanguage(" ENGLISH\t") == "en", "English locale normalization");
	check(SelectLanguage("cn") == "zhhant" && SelectLanguage("CN") == "zhhant" &&
		SelectLanguage("\t Cn \r\n") == "zhhant", "legacy Chinese archive suffix selects the Traditional Chinese catalog");
	constexpr std::array<std::string_view, 12> languages{
		"en", "ru", "de", "es", "esmx", "fr", "it", "ja", "pl", "ptbr", "zhhans", "zhhant"
	};
	for (const auto language : languages) {
		check(SelectLanguage(language) == language, "each packaged locale selects its own catalog");
		std::string normalizedInput = "\t ";
		for (const auto character : language) {
			normalizedInput.push_back(static_cast<char>(character - ('a' - 'A')));
		}
		normalizedInput += "\r\n";
		check(SelectLanguage(normalizedInput) == language, "case and surrounding whitespace preserve locale selection");
	}
	for (const auto unsupported : { "", "ko", "pt", "zh", "zhans", "zhant", "es-mx", "en_US",
		"../ru", "..\\ja", "C:\\fr", "zhhans.tsv", "/pl", "ja/../../en", "ru:stream", "ja ja", "ptbr/" }) {
		check(SelectLanguage(unsupported) == "en", "empty, unsupported or path-like locale selects English");
	}
	check(SelectLanguage(std::string_view("ja\0/../../ru", 12)) == "en", "embedded NUL does not bypass the locale allowlist");

	const auto japanese = ParseCatalog(
		"$Clipboard_Imported\t{2}秒でオブジェクト{0}個とワイヤー{1}本をインポートしました。\n"
		"$Clipboard_Empty\t空のスロット\n");
	const auto simplifiedChinese = ParseCatalog("$Clipboard_Empty\t空槽位\n");
	const auto traditionalChinese = ParseCatalog("$Clipboard_Empty\t空欄位\n");
	check(japanese && simplifiedChinese && traditionalChinese, "Japanese and both Chinese catalogs accept UTF-8");
	check(Resolve(english.entries, japanese.entries, "$Clipboard_Imported", arguments) ==
		"83.531秒でオブジェクト1194個とワイヤー25本をインポートしました。", "Japanese formatting preserves multibyte text around reordered arguments");
	check(Resolve(english.entries, simplifiedChinese.entries, "$Clipboard_Empty", {}) == "空槽位" &&
		Resolve(english.entries, traditionalChinese.entries, "$Clipboard_Empty", {}) == "空欄位", "Chinese script variants retain distinct UTF-8 values");
	check(Resolve(english.entries, japanese.entries, "$Clipboard_Repeated", arguments) == "1194 / 1194",
		"a partial CJK catalog keeps English per-key fallback");

	const auto rejected = [&](std::string_view bytes, const char* description) {
		const auto result = ParseCatalog(bytes);
		check(!result && result.entries.empty(), description);
	};
	rejected("$Clipboard_Valid\tEnglish\n$Clipboard_Bad\tBad\\q\n", "malformed escape atomically rejects previous valid rows");
	rejected("$Clipboard_Valid\tEnglish\n$Clipboard_Valid\tDuplicate\n", "duplicate key atomically rejects file");
	rejected("$Clipboard_Valid\tExtra\tcolumn\n", "unescaped tab rejected");
	rejected("$Clipboard_Valid\tEmbedded\rCR\n", "unescaped carriage return rejected");
	rejected("$Clipboard_Valid\t\n", "empty text rejected");
	rejected("$Clipboard_Valid\tEnds in \\", "trailing escape rejected");
	rejected("$Clipboard_Valid\t{6}\n", "unsupported placeholder rejected");
	rejected("$Clipboard_Valid\t{10}\n", "multi-digit placeholder rejected");
	rejected("$Clipboard_Valid\t{text}\n", "named placeholder rejected");
	rejected("$Clipboard_Valid\tUnclosed {0\n", "unfinished brace rejected");
	rejected("$Clipboard_Valid\tUnmatched }\n", "unmatched closing brace rejected");
	rejected("$Other_Valid\tEnglish\n", "foreign key prefix rejected");
	rejected("$Clipboard_../ru\tEnglish\n", "path punctuation rejected in key");
	rejected("# Empty\n\n", "empty catalog rejected");
	rejected(std::string("$Clipboard_Valid\tNull\0truncated\n", 32), "embedded NUL rejected");
	rejected("$Clipboard_Valid\t\xC0\xAF\n", "overlong two-byte UTF-8 rejected");
	rejected("$Clipboard_Valid\t\xE0\x80\xAF\n", "overlong three-byte UTF-8 rejected");
	rejected("$Clipboard_Valid\t\xED\xA0\x80\n", "UTF-8 surrogate codepoint rejected");
	rejected("$Clipboard_Valid\t\xF4\x90\x80\x80\n", "UTF-8 codepoint above Unicode maximum rejected");
	rejected("$Clipboard_Valid\t\xD0", "truncated multibyte character rejected");
	rejected("$Clipboard_Valid\t\x80\n", "bare UTF-8 continuation rejected");
	check(IsValidUtf8("English / Русский / Deutsch / español / français / italiano / 日本語 / polski / português / 简体中文 / 繁體中文 / \xF0\x9F\x8E\xAE"),
		"valid Latin diacritics, Cyrillic, CJK and four-byte Unicode preserved");
	rejected(std::string(kMaxCatalogBytes + 1, 'a'), "file-size bound enforced");
	rejected("$Clipboard_" + std::string(kMaxKeyBytes, 'A') + "\ttext\n", "key-size bound enforced");
	rejected("$Clipboard_Valid\t" + std::string(kMaxValueBytes + 1, 'a') + "\n", "decoded text bound enforced");
	std::string excessEntries;
	for (std::size_t index = 0; index <= kMaxEntries; ++index) {
		excessEntries += "$Clipboard_" + std::to_string(index) + "\tEntry\n";
	}
	rejected(excessEntries, "entry-count bound enforced");

	if (argc == 2) {
		const auto directory = std::filesystem::path(argv[1]);
		std::filesystem::create_directories(directory);
		const auto path = directory / "utf8-catalog.tsv";
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output << "$Clipboard_File\tОбъект {0}\n";
		}
		const auto loaded = ReadCatalog(path);
		check(loaded && Resolve(loaded.entries, {}, "$Clipboard_File", { "125", "", "", "", "", "" }) == "Объект 125",
			"binary file reader preserves Cyrillic through lookup and formatting");
		const auto cjkPath = directory / "utf8-cjk-catalog.tsv";
		{
			std::ofstream output(cjkPath, std::ios::binary | std::ios::trunc);
			output << "$Clipboard_File\t名前: {0}\\n对象 / 物件\n";
		}
		const auto cjkLoaded = ReadCatalog(cjkPath);
		check(cjkLoaded && Resolve(cjkLoaded.entries, {}, "$Clipboard_File", { "建築 {1}", "unused", "", "", "", "" }) ==
			"名前: 建築 {1}\n对象 / 物件", "binary file reader preserves CJK and inserts a multibyte user name once");
		check(!ReadCatalog(directory / "missing-catalog.tsv"), "missing file gives failure instead of partial/empty catalog");
	} else {
		check(false, "test-data directory argument required");
	}

	std::cout << checks << " localization checks, " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
