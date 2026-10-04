#include "RuntimeDatabasePreflight.h"

// RuntimeDatabase is deliberately an internal CommonLibF4RD type. The whole
// dependency is pinned, so using its loader makes this preflight accept exactly
// the format that F4SE::Init will consume. Include the umbrella first because
// individual CommonLibF4RD headers rely on its shared platform declarations.
#include <F4SE/F4SE.h>

#include "../../external/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.h"

#include <array>
#include <exception>
#include <string>
#include <utility>

namespace Clipboard::RuntimeDatabasePreflight
{
	ValidationGuard::ValidationGuard(
		std::unique_ptr<REL::RuntimeDatabase> a_database) noexcept :
		_database(std::move(a_database))
	{}

	ValidationGuard::ValidationGuard(ValidationGuard&&) noexcept = default;
	ValidationGuard& ValidationGuard::operator=(ValidationGuard&&) noexcept = default;
	ValidationGuard::~ValidationGuard() = default;

	ValidationResult ValidateAndHold(const std::filesystem::path& a_path)
	{
		ValidationResult result;
		result.databasePath = a_path;
		try {
			auto database = REL::RuntimeDatabase::load(a_path);
			if (!database) {
				result.error = "CommonLibF4RD returned no database after validation";
				return result;
			}

			result.recordCount = database->record_count();
			result.guard = std::unique_ptr<ValidationGuard>(
				new ValidationGuard(std::move(database)));
		} catch (const std::exception& exception) {
			result.error = exception.what();
		} catch (...) {
			result.error = "CommonLibF4RD validation failed with an unknown exception";
		}
		return result;
	}

	ValidationResult ValidateAndHoldRuntimeCandidates(
		const std::filesystem::path& a_pluginsDirectory,
		std::string_view a_runtimeVersion)
	{
		const std::array candidates{
			a_pluginsDirectory / "f4rd-runtime.bin",
			a_pluginsDirectory /
				("f4rd-runtime-" + std::string(a_runtimeVersion) + ".bin")
		};

		// Keep this predicate and order identical to pinned IDDatabase::load.
		// In particular, do not pre-filter on file type or size: CommonLib stops
		// at the first path that exists and treats a malformed entry as fatal.
		for (const auto& candidate : candidates) {
			std::error_code error;
			if (std::filesystem::exists(candidate, error) && !error) {
				return ValidateAndHold(candidate);
			}
		}

		ValidationResult result;
		result.error = "Runtime Database is missing; checked " +
		               candidates[0].string() + " and " + candidates[1].string();
		return result;
	}
}
