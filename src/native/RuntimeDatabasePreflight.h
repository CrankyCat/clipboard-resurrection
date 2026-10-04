#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace REL
{
	class RuntimeDatabase;
}

namespace Clipboard::RuntimeDatabasePreflight
{
	struct ValidationResult;

	// Retains the fully validated read-only mapping while CommonLibF4RD opens
	// and resolves through its process-wide database. This prevents an ordinary
	// writer from replacing the checked file in the validation/use window.
	class ValidationGuard final
	{
	public:
		ValidationGuard(const ValidationGuard&) = delete;
		ValidationGuard(ValidationGuard&&) noexcept;
		ValidationGuard& operator=(const ValidationGuard&) = delete;
		ValidationGuard& operator=(ValidationGuard&&) noexcept;
		~ValidationGuard();

	private:
		friend struct ValidationResult;
		friend ValidationResult ValidateAndHold(const std::filesystem::path& a_path);

		explicit ValidationGuard(
			std::unique_ptr<REL::RuntimeDatabase> a_database) noexcept;

		std::unique_ptr<REL::RuntimeDatabase> _database;
	};

	struct ValidationResult
	{
		std::unique_ptr<ValidationGuard> guard;
		std::size_t recordCount{};
		std::filesystem::path databasePath;
		std::string error;

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return guard != nullptr;
		}
	};

	// Runs the exact parser and structural validator from the pinned
	// CommonLibF4RD source. Parser exceptions become an ordinary plugin-load
	// failure instead of reaching IDDatabase initialization's fatal policy.
	[[nodiscard]] ValidationResult ValidateAndHold(
		const std::filesystem::path& a_path);

	// Mirrors pinned IDDatabase candidate selection: prefer the unversioned
	// database, then try the runtime-versioned name only when the first path
	// does not exist. An existing but invalid first candidate is never skipped.
	[[nodiscard]] ValidationResult ValidateAndHoldRuntimeCandidates(
		const std::filesystem::path& a_pluginsDirectory,
		std::string_view a_runtimeVersion);
}
