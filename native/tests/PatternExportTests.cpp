// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#include "AtomicFile.h"
#include "PatternExportValidation.h"
#include "PatternNumbers.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <bit>
#include <limits>
#include <locale>

namespace
{
	using Clipboard::AtomicFile::Stage;
	std::string Read(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
	}
	struct FailingTransaction
	{
		Stage failure;
		std::vector<Stage> calls;
		std::string destination{ "previous valid pattern" }, temporary;
		bool Step(Stage stage) { calls.push_back(stage); return stage != failure; }
		bool Create() { return Step(Stage::Create); }
		bool Write(std::string_view bytes) { temporary = bytes.substr(0, bytes.size() / 2); if (!Step(Stage::Write)) { return false; } temporary = bytes; return true; }
		bool Flush() { return Step(Stage::Flush); }
		bool Verify(std::string_view bytes) { return Step(Stage::Verify) && temporary == bytes; }
		bool Close() { return Step(Stage::Close); }
		bool Replace() { if (!Step(Stage::Replace)) { return false; } destination = temporary; return true; }
		std::uint32_t Error() const { return ERROR_WRITE_FAULT; }
	};
	std::string Pattern()
	{
		std::ostringstream pattern;
		pattern << "[general]\npattern_name=Fixture\ncopied_on=2026-09-18\ncharacter=Tester\n"
			"workshop_id=4079\nworkshop_plugin=0\nplugin_count=2\nwire_count=2\nobject_count=2\nclipboard_version=2.4.0\n"
			"[reference]\ncell_id=1\ncell_plugin=0\nposition_x=0\nposition_y=-1\nposition_z=100.5\n"
			"angle_x=0\nangle_y=0\nangle_z=0\n[plugins]\n0=Fallout4.esm\n1=DLCworkshop03.esm\n"
			"[wires]\n0=0|1\n[objects]\n0=0|1234|1|0|0|0|0|0|0\n1=1|1235|1|1|0|0|0|0|0\n";
		Clipboard::ExistingPower::Write(pattern,
			{ { {1, 4920}, {1, 17081}, {1, 1}, {0, 4079} } }, { {0, 0} });
		return pattern.str();
	}
	std::string Changed(std::string text, std::string_view from, std::string_view to)
	{
		const auto at = text.find(from);
		if (at == text.npos) { throw std::runtime_error("bad test fixture replacement"); }
		text.replace(at, from.size(), to);
		return text;
	}
}

int main(int argc, char** argv)
{
	int checks{}, failures{};
	const auto check = [&](bool success, const char* name) {
		++checks;
		if (!success) { ++failures; std::cerr << name << '\n'; }
	};
	using namespace Clipboard;
	using namespace Clipboard::PatternNumbers;
	const auto sameFloat = [](float left, float right) {
		return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
	};
	const long double pi = std::acos(-1.0L);
	for (const float value : { 0.0F, -0.0F, 0.000001F, 0.000000123456F, 0.123456F,
		1.234567F, -12.345678F, 179.999999F, 123456.789F,
		std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::max() }) {
		check(sameFloat(Parse<float>(Format(value)), value), "pattern float round-trip retains every stored bit");
		check(Format(value).find_first_of("eE,") == std::string::npos, "fixed ASCII-dot pattern representation");
	}
	check(Parse<float>(" +1.234567 ") == 1.234567F && Parse<float>("1.234567e-2") == 0.01234567F,
		"legacy fixed, whitespace, positive sign and exponent values remain readable");
	for (const auto text : { "nan", "inf", "1.25x", "1,25", "1e999", "" }) {
		check(Parse<float>(text) == 0.0F, "malformed/nonfinite pattern numbers cannot become partial values");
	}
	// Include six-place decimal degrees and many representable engine radian values.
	for (int index = -10000; index <= 10000; ++index) {
		const float radians = static_cast<float>(index) * 0.0001234567F;
		const auto degrees = static_cast<long double>(radians) / pi * 180.0L;
		check(sameFloat(Radians(Format(degrees), pi), radians), "degree file conversion preserves float radians");
		const float coordinate = static_cast<float>(index) * 12.345678F;
		check(sameFloat(Parse<float>(Format(coordinate)), coordinate), "world-coordinate file round-trip has no decimal truncation");
	}
	for (const auto text : { "0.000001", "12.345678", "179.999999", "-0.000001" }) {
		const float expected = static_cast<float>(static_cast<long double>(Parse<double>(text)) / 180.0L * pi);
		check(sameFloat(Radians(text, pi), expected), "six-place external angle imports without intermediate float degrees");
	}
	struct CommaDecimal : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
	const auto previousLocale = std::locale();
	std::locale::global(std::locale(previousLocale, new CommaDecimal));
	check(Format(1.25F) == "1.25" && Parse<float>("1.25") == 1.25F, "pattern numbers independent of system decimal locale");
	std::locale::global(previousLocale);
	const PatternExport::Counts counts{ 2, 2, 1, 1, 1 };
	const auto valid = Pattern();
	check(PatternExport::Validate(valid, counts), "complete snapshot with internal and existing-endpoint wires validates");
	check(!PatternExport::Validate(valid, {0, 2, 0, 0, 0}), "zero-object snapshots cannot be committed");
	auto empty = Changed(valid, "object_count=2", "object_count=0");
	empty = Changed(empty, "wire_count=2", "wire_count=0");
	empty.erase(empty.find("[wires]"));
	empty += "[objects]\n";
	check(!PatternExport::Validate(empty, {0, 2, 0, 0, 0}), "otherwise valid empty pattern is rejected");
	auto created = Changed(valid, "0=1|4920|1|17081|1|1|0|4079|0|0|0|0", "0=0|4279383126|1|17081|1|1|0|4079|1|0|0|0");
	check(PatternExport::Validate(created, counts), "validated export accepts same-save external player-created references");
	check(!PatternExport::Validate(valid, { 3, 2, 1, 1, 1 }), "filtered object count must equal actual exported rows");
	check(!PatternExport::Validate(Changed(valid, "object_count=2", "object_count=3"), counts), "declared object count mismatch rejected");
	check(!PatternExport::Validate(Changed(valid, "wire_count=2", "wire_count=1"), counts), "external wires included in total count");
	check(!PatternExport::Validate(Changed(valid, "0=0|1", "0=0|2"), counts), "out-of-range internal wire rejected");
	check(!PatternExport::Validate(Changed(valid, "0=0|1", "0=0|0"), counts), "invalid self wire rejected");
	check(!PatternExport::Validate(Changed(valid, "1=1|1235", "1=2|1235"), counts), "invalid object plugin rejected");
	check(!PatternExport::Validate(Changed(valid, "1=1|1235", "4=1|1235"), counts), "physical row order cannot contain holes");
	check(!PatternExport::Validate(Changed(valid, "1=1|1235", "0=1|1235"), counts), "duplicate object row rejected");
	check(!PatternExport::Validate(Changed(valid, "position_z=100.5", "position_z=nan"), counts), "nonfinite transform rejected");
	check(!PatternExport::Validate(Changed(valid, "pattern_name=Fixture", "pattern_name=Fixture\n[general]"), counts), "name cannot inject sections");
	check(!PatternExport::Validate(valid + "[extra]\nx=y\n", counts), "unexpected injected section rejected");
	check(!PatternExport::Validate(Changed(valid, "0=1|4920", "0=2|4920"), counts), "existing endpoint plugin range validated");
	check(!PatternExport::Validate(Changed(valid, "[external_wires]\n0=0|0", "[external_wires]\n0=2|0"), counts), "external source row range validated");
	check(!PatternExport::Validate(Changed(valid, "[external_wires]\n0=0|0", "[external_wires]\n0=0|1"), counts), "external target row range validated");
	check(!PatternExport::Validate(valid.substr(0, valid.size() - 3), counts), "truncated endpoint serialization rejected");
	std::istringstream roundtrip(valid);
	const auto power = ExistingPower::Read(roundtrip);
	check(power.valid && power.endpoints.size() == 1 && power.wires.size() == 1 &&
		power.endpoints[0]->reference.local == 4920 && power.wires[0]->objectRow == 0,
		"existing-endpoint writer and reader retain endpoint identity and row");
	for (const auto stage : { Stage::Create, Stage::Write, Stage::Flush, Stage::Verify, Stage::Close, Stage::Replace }) {
		FailingTransaction transaction{ stage, {}, "previous valid pattern", {} };
		const auto result = AtomicFile::Commit(transaction, valid);
		check(!result && result.stage == stage && result.error == ERROR_WRITE_FAULT, "specific failed stage reported");
		check(transaction.destination == "previous valid pattern", "all precommit failures preserve old pattern bytes");
		check(transaction.calls.back() == stage, "no commit or later operations after a failure");
	}
	FailingTransaction successful{ Stage::Complete, {}, "previous valid pattern", {} };
	check(static_cast<bool>(AtomicFile::Commit(successful, valid)) && successful.destination == valid,
		"successful sequence publishes only complete serialized contents");

	if (argc != 2) { std::cerr << "test output directory required\n"; return 2; }
	const auto root = std::filesystem::path(argv[1]) / (L"run-" + std::to_wstring(GetCurrentProcessId()));
	std::filesystem::create_directories(root);
	const auto path = root / L"pattern space 日本語.ini";
	const auto old = std::string("old pattern\r\nwith bytes\0after nul", 33);
	check(static_cast<bool>(AtomicFile::Write(path, old)) && Read(path) == old, "new Unicode/space path written exactly");
	check(static_cast<bool>(AtomicFile::Write(path, valid)) && Read(path) == valid, "existing pattern atomically replaced");
	check(static_cast<bool>(AtomicFile::Write(path, old)), "restore protected test fixture");
	HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	check(locked != INVALID_HANDLE_VALUE, "destination held without delete sharing for replacement failure");
	const auto denied = AtomicFile::Write(path, valid);
	check(!denied && denied.stage == Stage::Replace && Read(path) == old, "real locked-destination replacement failure preserves bytes");
	if (locked != INVALID_HANDLE_VALUE) { CloseHandle(locked); }
	check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY) != 0, "read-only destination fixture established");
	const auto readOnly = AtomicFile::Write(path, valid);
	check(!readOnly && readOnly.stage == Stage::Replace && Read(path) == old, "real read-only replacement failure preserves bytes");
	SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
	const auto missing = root / L"missing directory" / L"pattern.ini";
	check(!AtomicFile::Write(missing, valid) && !std::filesystem::exists(missing), "missing directory fails without fallback location");
	check(!AtomicFile::Write({}, valid), "empty path rejected");
	std::size_t entries{};
	for (const auto& entry : std::filesystem::directory_iterator(root)) { (void)entry; ++entries; }
	check(entries == 1, "temporary files removed after successful and failed replacements");
	check(Read(path) == old, "failed operations never change prior bytes");
	std::filesystem::remove(path);
	std::filesystem::remove(root);
	std::cout << checks << " pattern export checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
