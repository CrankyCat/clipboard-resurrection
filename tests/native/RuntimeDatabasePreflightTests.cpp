#include "RuntimeDatabasePreflight.h"
#include "LoggingTests.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
	constexpr std::array<std::uint8_t, 8> MAGIC{
		'F', '4', 'R', 'D', 'B', 'I', 'N', 0
	};
	constexpr std::size_t HEADER_SIZE = 112;
	constexpr std::size_t RECORD_SIZE = 40;

	template <class T>
	void WriteLE(
		std::vector<std::uint8_t>& a_bytes,
		std::size_t a_offset,
		T a_value)
	{
		static_assert(std::is_unsigned_v<T>);
		for (std::size_t index = 0; index < sizeof(T); ++index) {
			a_bytes.at(a_offset + index) =
				static_cast<std::uint8_t>(a_value >> (index * 8));
		}
	}

	[[nodiscard]] std::uint32_t CRC32(std::span<const std::uint8_t> a_bytes)
	{
		std::uint32_t crc = 0xFFFFFFFFu;
		for (const auto byte : a_bytes) {
			crc ^= byte;
			for (unsigned bit = 0; bit < 8; ++bit) {
				const auto mask = static_cast<std::uint32_t>(0u - (crc & 1u));
				crc = (crc >> 1) ^ (0xEDB88320u & mask);
			}
		}
		return ~crc;
	}

	[[nodiscard]] std::vector<std::uint8_t> MakeDatabase(
		std::uint32_t a_recordFlags = 0,
		bool a_legacyPrefix = false)
	{
		std::vector<std::uint8_t> runtime(HEADER_SIZE + RECORD_SIZE);
		std::ranges::copy(MAGIC, runtime.begin());
		WriteLE<std::uint16_t>(runtime, 8, 1);
		WriteLE<std::uint16_t>(runtime, 10, 5);
		WriteLE<std::uint32_t>(
			runtime, 12, static_cast<std::uint32_t>(HEADER_SIZE));
		WriteLE<std::uint32_t>(runtime, 20, 0x01020304u);
		WriteLE<std::uint32_t>(runtime, 24, 1);

		WriteLE<std::uint64_t>(runtime, 48, HEADER_SIZE);
		for (const auto offset : {
				 std::size_t{ 56 }, std::size_t{ 64 }, std::size_t{ 72 },
				 std::size_t{ 80 }, std::size_t{ 88 } }) {
			WriteLE<std::uint64_t>(runtime, offset, runtime.size());
		}
		WriteLE<std::uint64_t>(runtime, 96, runtime.size());

		const auto record = HEADER_SIZE;
		WriteLE<std::uint64_t>(runtime, record, 1);
		WriteLE<std::uint32_t>(runtime, record + 32, a_recordFlags);
		WriteLE<std::uint32_t>(
			runtime,
			104,
			CRC32(std::span<const std::uint8_t>(runtime).subspan(HEADER_SIZE)));

		if (!a_legacyPrefix) {
			return runtime;
		}
		std::vector<std::uint8_t> combined(sizeof(std::uint64_t), 0);
		combined.insert(combined.end(), runtime.begin(), runtime.end());
		return combined;
	}

	[[nodiscard]] bool WriteBytes(
		const std::filesystem::path& a_path,
		std::span<const std::uint8_t> a_bytes)
	{
		std::ofstream stream(a_path, std::ios::binary | std::ios::trunc);
		if (!stream) {
			return false;
		}
		stream.write(
			reinterpret_cast<const char*>(a_bytes.data()),
			static_cast<std::streamsize>(a_bytes.size()));
		return static_cast<bool>(stream);
	}

	class Tests
	{
	public:
		void Expect(bool a_condition, std::string_view a_name)
		{
			if (!a_condition) {
				++_failures;
				std::cerr << "FAIL: " << a_name << '\n';
			}
		}

		[[nodiscard]] int failures() const noexcept { return _failures; }

	private:
		int _failures{};
	};
}

int main(int a_argc, char** a_argv)
{
	Tests tests;
	CheckLogging(tests);
	if (a_argc == 3 && std::string_view(a_argv[1]) == "--validate") {
		const auto result =
			Clipboard::RuntimeDatabasePreflight::ValidateAndHold(a_argv[2]);
		if (!result) {
			std::cerr << "Runtime Database rejected: " << result.error << '\n';
			return 1;
		}
		std::cout << "Runtime Database accepted with " << result.recordCount
				  << " records\n";
		return 0;
	}
	if (a_argc != 2) {
		std::cerr << "Expected a test-data directory or --validate <database>\n";
		return 2;
	}

	const std::filesystem::path testRoot(a_argv[1]);
	std::error_code error;
	std::filesystem::create_directories(testRoot, error);
	if (error) {
		std::cerr << "Could not create test-data directory: "
				  << error.message() << '\n';
		return 2;
	}

	const std::array paths{
		testRoot / "valid.bin",
		testRoot / "valid-legacy.bin",
		testRoot / "truncated.bin",
		testRoot / "unsupported.bin",
		testRoot / "checksum-invalid.bin",
		testRoot / "structure-invalid.bin"
	};

	const auto valid = MakeDatabase();
	tests.Expect(WriteBytes(paths[0], valid), "write valid database");
	{
		const auto result =
			Clipboard::RuntimeDatabasePreflight::ValidateAndHold(paths[0]);
		tests.Expect(static_cast<bool>(result), "valid database passes");
		tests.Expect(result.recordCount == 1, "valid database record count");
		tests.Expect(result.error.empty(), "valid database has no error");

		const auto writer = ::CreateFileW(
			paths[0].c_str(),
			GENERIC_WRITE,
			FILE_SHARE_READ,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		tests.Expect(
			writer == INVALID_HANDLE_VALUE &&
				::GetLastError() == ERROR_SHARING_VIOLATION,
			"validation guard prevents writes during CommonLib initialization");
		if (writer != INVALID_HANDLE_VALUE) {
			(void)::CloseHandle(writer);
		}
	}

	const auto validLegacy = MakeDatabase(0, true);
	tests.Expect(
		WriteBytes(paths[1], validLegacy),
		"write legacy-prefixed database");
	{
		const auto result =
			Clipboard::RuntimeDatabasePreflight::ValidateAndHold(paths[1]);
		tests.Expect(
			static_cast<bool>(result), "legacy-prefixed database passes");
		tests.Expect(
			result.recordCount == 1, "legacy-prefixed database record count");
	}

	std::vector<std::uint8_t> truncated(MAGIC.begin(), MAGIC.end());
	tests.Expect(
		WriteBytes(paths[2], truncated), "write truncated database");
	{
		const auto result =
			Clipboard::RuntimeDatabasePreflight::ValidateAndHold(paths[2]);
		tests.Expect(!result, "truncated database fails nonfatally");
		tests.Expect(
			!result.error.empty(), "truncated database reports an error");
	}

	auto unsupported = valid;
	WriteLE<std::uint16_t>(unsupported, 8, 2);
	tests.Expect(
		WriteBytes(paths[3], unsupported), "write unsupported database");
	{
		const auto result =
			Clipboard::RuntimeDatabasePreflight::ValidateAndHold(paths[3]);
		tests.Expect(!result, "unsupported database fails nonfatally");
		tests.Expect(
			!result.error.empty(), "unsupported database reports an error");
	}

	auto checksumInvalid = valid;
	checksumInvalid.back() ^= 0xFFu;
	tests.Expect(
		WriteBytes(paths[4], checksumInvalid),
		"write checksum-invalid database");
	{
		const auto result =
			Clipboard::RuntimeDatabasePreflight::ValidateAndHold(paths[4]);
		tests.Expect(!result, "checksum-invalid database fails nonfatally");
		tests.Expect(
			!result.error.empty(),
			"checksum-invalid database reports an error");
	}

	const auto structureInvalid = MakeDatabase(0x80000000u);
	tests.Expect(
		WriteBytes(paths[5], structureInvalid),
		"write structure-invalid database");
	{
		const auto result =
			Clipboard::RuntimeDatabasePreflight::ValidateAndHold(paths[5]);
		tests.Expect(!result, "structure-invalid database fails nonfatally");
		tests.Expect(
			!result.error.empty(),
			"structure-invalid database reports an error");
	}

	{
		const auto result = Clipboard::RuntimeDatabasePreflight::ValidateAndHold(
			testRoot / "missing.bin");
		tests.Expect(!result, "missing database fails nonfatally");
		tests.Expect(
			!result.error.empty(), "missing database reports an error");
	}

	const auto candidateRoot = testRoot / "candidates";
	std::filesystem::create_directories(candidateRoot, error);
	if (error) {
		std::cerr << "Could not create candidate test directory: "
				  << error.message() << '\n';
		return 2;
	}
	constexpr std::string_view runtimeVersion = "1-11-240-0";
	const auto unversionedCandidate = candidateRoot / "f4rd-runtime.bin";
	const auto versionedCandidate =
		candidateRoot / "f4rd-runtime-1-11-240-0.bin";

	// Remove only these two fixed test fixtures so an interrupted earlier run
	// cannot change candidate precedence on the next invocation.
	std::filesystem::remove(unversionedCandidate, error);
	error.clear();
	std::filesystem::remove(versionedCandidate, error);
	error.clear();

	// IDDatabase::load falls back to the versioned filename only when the
	// preferred unversioned path does not exist.
	tests.Expect(
		WriteBytes(versionedCandidate, valid),
		"write versioned fallback database");
	{
		const auto result = Clipboard::RuntimeDatabasePreflight::
			ValidateAndHoldRuntimeCandidates(candidateRoot, runtimeVersion);
		tests.Expect(
			static_cast<bool>(result), "versioned fallback database passes");
		tests.Expect(
			result.databasePath == versionedCandidate,
			"missing unversioned database selects versioned fallback");
		tests.Expect(
			result.recordCount == 1, "versioned fallback database record count");
	}
	std::filesystem::remove(versionedCandidate, error);
	error.clear();

	// When both files exist, the unversioned database must win.
	tests.Expect(
		WriteBytes(unversionedCandidate, valid),
		"write preferred unversioned database");
	tests.Expect(
		WriteBytes(versionedCandidate, valid),
		"write secondary versioned database");
	{
		const auto result = Clipboard::RuntimeDatabasePreflight::
			ValidateAndHoldRuntimeCandidates(candidateRoot, runtimeVersion);
		tests.Expect(
			static_cast<bool>(result), "preferred unversioned database passes");
		tests.Expect(
			result.databasePath == unversionedCandidate,
			"unversioned database takes precedence");
	}
	std::filesystem::remove(unversionedCandidate, error);
	error.clear();
	std::filesystem::remove(versionedCandidate, error);
	error.clear();

	// An invalid preferred file must not silently fall through to a valid
	// runtime-specific file that CommonLibF4RD itself would never reach.
	const std::vector<std::uint8_t> empty;
	tests.Expect(
		WriteBytes(unversionedCandidate, empty),
		"write empty preferred database");
	tests.Expect(
		WriteBytes(versionedCandidate, valid),
		"write valid but unreachable versioned database");
	{
		const auto result = Clipboard::RuntimeDatabasePreflight::
			ValidateAndHoldRuntimeCandidates(candidateRoot, runtimeVersion);
		tests.Expect(
			!result, "invalid existing unversioned database fails nonfatally");
		tests.Expect(
			result.databasePath == unversionedCandidate,
			"invalid unversioned database does not fall through");
		tests.Expect(
			!result.error.empty(), "invalid unversioned database reports an error");
	}
	std::filesystem::remove(unversionedCandidate, error);
	error.clear();
	std::filesystem::remove(versionedCandidate, error);
	error.clear();

	{
		const auto result = Clipboard::RuntimeDatabasePreflight::
			ValidateAndHoldRuntimeCandidates(candidateRoot, runtimeVersion);
		tests.Expect(!result, "missing candidate set fails nonfatally");
		tests.Expect(
			result.databasePath.empty(),
			"missing candidate set has no selected path");
		tests.Expect(
			result.error.find(unversionedCandidate.string()) != std::string::npos &&
				result.error.find(versionedCandidate.string()) != std::string::npos,
			"missing candidate error names both checked paths");
	}

	for (const auto& path : paths) {
		std::filesystem::remove(path, error);
		error.clear();
	}
	std::filesystem::remove(candidateRoot, error);
	error.clear();
	std::filesystem::remove(testRoot, error);

	if (tests.failures() != 0) {
		std::cerr << tests.failures()
				  << " Runtime Database preflight test(s) failed\n";
		return 1;
	}
	std::cout << "Runtime Database preflight safely matched CommonLibF4RD "
				 "candidate selection and rejected malformed inputs\n";
	return 0;
}
