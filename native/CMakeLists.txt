cmake_minimum_required(VERSION 4.2)

macro(set_from_environment variable)
	if(NOT DEFINED ${variable} AND DEFINED ENV{${variable}})
		set(${variable} "$ENV{${variable}}")
	endif()
endmacro()

set_from_environment(VCPKG_ROOT)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED VCPKG_ROOT)
	set(
		CMAKE_TOOLCHAIN_FILE
		"${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
		CACHE FILEPATH "vcpkg CMake toolchain"
	)
endif()

set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "vcpkg target triplet")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "MSVC runtime")

# The manifest is the single product-version source for CMake and the modern
# PowerShell build/stage/PE metadata helpers.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json" CLIPBOARD_MANIFEST)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json")
string(JSON CLIPBOARD_VERSION GET "${CLIPBOARD_MANIFEST}" "version-string")
if(NOT CLIPBOARD_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
	message(FATAL_ERROR "Clipboard requires a major.minor.patch product version")
endif()
project(Clipboard VERSION ${CLIPBOARD_VERSION} LANGUAGES CXX RC)
if(PROJECT_VERSION_MAJOR GREATER 255 OR PROJECT_VERSION_MINOR GREATER 255 OR PROJECT_VERSION_PATCH GREATER 4095)
	message(FATAL_ERROR "Clipboard product version exceeds the F4SE packed-version field bounds")
endif()
# Windows resources and the F4SE packed version retain a zero fourth component.
set(PROJECT_VERSION_TWEAK 0)

set(CLIPBOARD_BUILD_RECORD "${CMAKE_CURRENT_SOURCE_DIR}/../tools/Clipboard.InternalBuild.json")
file(READ "${CLIPBOARD_BUILD_RECORD}" CLIPBOARD_BUILD_JSON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CLIPBOARD_BUILD_RECORD}")
string(JSON CLIPBOARD_INTERNAL_BUILD GET "${CLIPBOARD_BUILD_JSON}" "internalBuild")
if(NOT CLIPBOARD_INTERNAL_BUILD MATCHES "^[1-9][0-9]*$" OR CLIPBOARD_INTERNAL_BUILD LESS 100 OR CLIPBOARD_INTERNAL_BUILD GREATER 2147483646)
    message(FATAL_ERROR "Invalid Clipboard internal build number")
endif()

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(FATAL_ERROR "In-source builds are not supported. Use a CMake preset.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
# MSVC 19.52 produced a failing host executable from the same /GL object only
# with /LTCG:incremental, including a fresh IPDB. Explicit /LTCG overrides the
# Visual Studio default while retaining whole-program and Release optimization.
if(MSVC)
	add_link_options("$<$<NOT:$<CONFIG:Debug>>:/LTCG>")
endif()
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME OFF CACHE BOOL "")

option(CLIPBOARD_BUILD_TESTS "Build v240 host checks" ON)

find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)

if(MSVC AND NOT CMAKE_GENERATOR MATCHES "Ninja")
	add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/MP>")
endif()

set(COMMONLIBF4RD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/CommonLibF4RD")
if(NOT EXISTS "${COMMONLIBF4RD_ROOT}/CommonLibF4/CMakeLists.txt")
	message(FATAL_ERROR "Pinned CommonLibF4RD source is missing at ${COMMONLIBF4RD_ROOT}")
endif()

set(COMMONLIBF4RD_COMMIT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/CommonLibF4RD.commit")
set(COMMONLIBF4RD_SNAPSHOT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/CommonLibF4RD.snapshot.json")
if(NOT EXISTS "${COMMONLIBF4RD_COMMIT_FILE}" OR
	NOT EXISTS "${COMMONLIBF4RD_SNAPSHOT_FILE}")
	message(FATAL_ERROR "CommonLibF4RD commit or snapshot metadata is missing")
endif()

file(STRINGS "${COMMONLIBF4RD_COMMIT_FILE}"
	CLIPBOARD_EXPECTED_COMMONLIB_COMMIT LIMIT_COUNT 1)

add_subdirectory(
	"${COMMONLIBF4RD_ROOT}/CommonLibF4"
	"${CMAKE_CURRENT_BINARY_DIR}/CommonLibF4"
	EXCLUDE_FROM_ALL
)

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

configure_file(cmake/Version.h.in generated/Version.h @ONLY)
configure_file(cmake/version.rc.in generated/version.rc @ONLY)

add_library(Clipboard SHARED
	src/PCH.h
	src/LegacyVMArray.h
	src/LegacyPapyrusArray.h
	src/LegacyCompat.h
	src/MasterPluginBlacklist.h
	src/NativeCallBoundary.h
	src/StartupCallbacks.h
	src/SettingsNumbers.h
	src/ScrapTargets.h
	src/ImportTransform.h
	src/OptionalFiltering.h
	src/OptionalFiltering.cpp
	src/OptionalFilteringPolicy.h
	src/OptionalChildOwnership.h
	src/AttachedScriptCandidates.h
	src/OptionalLinkedRefKeywords.h
	src/CachedReadLock.h
	src/Localization.h
	src/Localization.cpp
	src/LocalizationRuntime.h
	src/LocalizationRuntime.cpp
	src/PatternWireRows.h
	src/AtomicFile.h
	src/AtomicFile.cpp
	src/PatternExportValidation.h
	src/ExistingPowerEndpoints.h
	src/ExistingPowerEndpoints.inl
	src/EngineAPI.h
	src/PapyrusArgument.h
	src/PapyrusBinding.h
	src/PapyrusDispatchABI.h
	src/EngineSymbols.h
	src/EngineAPI.cpp
	src/LatentBridge.h
	src/F4SEObjectInterface.h
	src/SerializationNames.h
	src/SerializationValues.h
	src/LatentBridge.cpp
	src/ImportFunctors.inl
	src/EngineTaskDispatch.h
	src/ClipboardInputState.h
	src/ClipboardInputService.h
	src/ClipboardInputService.cpp
	src/ClipboardInputUI.h
	src/ClipboardInputUI.cpp
	src/EnginePowerFunctor.inl
	src/PowerTaskState.h
	src/PowerWireFunctor.inl
	src/PowerWorkJobs.inl
	src/ImportAnimation.h
	src/ImportAnimation.cpp
	src/ImportJobState.h
	src/WorkshopCallbackState.h
	src/WorkshopWaitSnapshots.h
	src/WorkshopCallbackLimit.h
	src/WorkshopCallbackDrain.h
	src/WorkshopCallbackSlice.h
	src/WorkshopCallbackFunctors.inl
	src/WorkshopNoOp.cpp
	src/WorkshopNoOp.h
	src/WorkshopNoOpPolicy.h
	src/ResourceStreamABI.h
	src/MotionTypeABI.h
	src/ConduitConnections.h
	src/ComponentRecipeMemo.h
	src/ComponentInventory.h
	src/FirstReferenceRows.h
	src/SelectionGeometry.h
	src/ImportCursorPayload.h
	src/ConduitConnections.cpp
	src/ConduitPairTrace.h
	src/ConduitPairTrace.inl
	src/ConduitAssemblyPlan.h
	src/ConduitAssemblyPlan.inl
	src/RuntimeDatabasePreflight.h
	src/RuntimeDatabasePreflight.cpp
	src/RuntimeCompatibility.h
	src/RuntimeSymbols.h
	src/Clipboard.cpp
	"${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
)

target_precompile_headers(Clipboard PRIVATE src/PCH.h)
target_compile_features(Clipboard PRIVATE cxx_std_20)
target_compile_definitions(Clipboard PRIVATE
	NOMINMAX
	UNICODE
	_UNICODE
	WIN32_LEAN_AND_MEAN
)
target_include_directories(Clipboard PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}/generated"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(Clipboard PRIVATE
	CommonLibF4::CommonLibF4
	fmt::fmt
	spdlog::spdlog
	bcrypt
)

set_target_properties(Clipboard PROPERTIES
	OUTPUT_NAME "clipboard"
	PDB_NAME "clipboard"
)

if(MSVC)
	target_compile_options(Clipboard PRIVATE
		"$<$<COMPILE_LANGUAGE:CXX>:/sdl;/utf-8;/Zi;/permissive-;/Zc:preprocessor;/W4;/WX;/wd4100;/wd4244>"
	)
	target_link_options(Clipboard PRIVATE
		/WX
		"$<$<CONFIG:Debug>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
		"$<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
	)
endif()

include(GNUInstallDirs)
install(TARGETS Clipboard RUNTIME DESTINATION F4SE/Plugins COMPONENT Runtime)

set(
	CLIPBOARD_STAGE_DIR
	"${CMAKE_CURRENT_SOURCE_DIR}/../build/stage/v240"
	CACHE PATH
	"Safe local directory used for the OG/NG/AE test package"
)

add_custom_target(stage
	COMMAND "${CMAKE_COMMAND}"
		--install "${CMAKE_CURRENT_BINARY_DIR}"
		--config "$<CONFIG>"
		--prefix "${CLIPBOARD_STAGE_DIR}"
		--component Runtime
	DEPENDS Clipboard
	USES_TERMINAL
	VERBATIM
)

if(CLIPBOARD_BUILD_TESTS)
	include(CTest)
	add_test(
		NAME clipboard_v240_commonlib_integrity
		COMMAND "${POWERSHELL_EXECUTABLE}"
			-NoProfile
			-ExecutionPolicy Bypass
			-File "${CMAKE_CURRENT_SOURCE_DIR}/../tools/Test-V240VendoredDependency.ps1"
			-ProjectRoot "${CMAKE_CURRENT_SOURCE_DIR}/.."
	)
	set_tests_properties(clipboard_v240_commonlib_integrity PROPERTIES TIMEOUT 60)

	add_executable(ClipboardRuntimeDatabasePreflightTests EXCLUDE_FROM_ALL
		tests/RuntimeDatabasePreflightTests.cpp
		src/RuntimeDatabasePreflight.cpp
	)
	target_compile_features(ClipboardRuntimeDatabasePreflightTests PRIVATE cxx_std_20)
	target_compile_definitions(ClipboardRuntimeDatabasePreflightTests PRIVATE
		NOMINMAX
		UNICODE
		_UNICODE
		WIN32_LEAN_AND_MEAN
	)
	target_include_directories(ClipboardRuntimeDatabasePreflightTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
	)
	target_link_libraries(ClipboardRuntimeDatabasePreflightTests PRIVATE
		CommonLibF4::CommonLibF4
		fmt::fmt
		spdlog::spdlog
	)
	if(MSVC)
		target_compile_options(ClipboardRuntimeDatabasePreflightTests PRIVATE
			/sdl
			/utf-8
			/permissive-
			/Zc:preprocessor
			/W4
			/WX
			/wd4100
		)
		target_link_options(ClipboardRuntimeDatabasePreflightTests PRIVATE /WX)
	endif()
	add_test(
		NAME clipboard_runtime_database_preflight
		COMMAND "$<TARGET_FILE:ClipboardRuntimeDatabasePreflightTests>"
			"${CMAKE_CURRENT_BINARY_DIR}/test-data/runtime-database-preflight"
	)
	set_tests_properties(clipboard_runtime_database_preflight PROPERTIES TIMEOUT 60)

	add_executable(ClipboardRuntimeCompatibilityTests EXCLUDE_FROM_ALL
		tests/RuntimeCompatibilityTests.cpp
		src/RuntimeDatabasePreflight.cpp
	)
	target_compile_features(ClipboardRuntimeCompatibilityTests PRIVATE cxx_std_20)
	target_compile_definitions(ClipboardRuntimeCompatibilityTests PRIVATE
		NOMINMAX
		UNICODE
		_UNICODE
		WIN32_LEAN_AND_MEAN
	)
	target_include_directories(ClipboardRuntimeCompatibilityTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
	)
	target_link_libraries(ClipboardRuntimeCompatibilityTests PRIVATE
		CommonLibF4::CommonLibF4
		fmt::fmt
		spdlog::spdlog
	)
	if(MSVC)
		target_compile_options(ClipboardRuntimeCompatibilityTests PRIVATE
			"$<$<COMPILE_LANGUAGE:CXX>:/sdl;/utf-8;/permissive-;/Zc:preprocessor;/W4;/WX;/wd4100>"
		)
		target_link_options(ClipboardRuntimeCompatibilityTests PRIVATE /WX)
	endif()
	add_test(
		NAME clipboard_runtime_compatibility
		COMMAND "$<TARGET_FILE:ClipboardRuntimeCompatibilityTests>"
	)
	set_tests_properties(clipboard_runtime_compatibility PROPERTIES TIMEOUT 60)

	add_executable(ClipboardLegacyCompatTests EXCLUDE_FROM_ALL
		tests/LegacyCompatTests.cpp
	)
	target_compile_features(ClipboardLegacyCompatTests PRIVATE cxx_std_20)
	target_compile_definitions(ClipboardLegacyCompatTests PRIVATE
		NOMINMAX
		UNICODE
		_UNICODE
		WIN32_LEAN_AND_MEAN
	)
	target_include_directories(ClipboardLegacyCompatTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
	)
	if(MSVC)
		target_compile_options(ClipboardLegacyCompatTests PRIVATE
			/sdl
			/utf-8
			/permissive-
			/Zc:preprocessor
			/W4
			/WX
			/wd4100
		)
		target_link_options(ClipboardLegacyCompatTests PRIVATE /WX)
	endif()
	add_test(
		NAME clipboard_legacy_compat
		COMMAND "$<TARGET_FILE:ClipboardLegacyCompatTests>"
	)
	set_tests_properties(clipboard_legacy_compat PROPERTIES TIMEOUT 60)

	add_executable(ClipboardImportJobTests EXCLUDE_FROM_ALL tests/ImportJobTests.cpp)
	add_executable(ClipboardConduitConnectionPolicyTests EXCLUDE_FROM_ALL tests/ConduitConnectionPolicyTests.cpp)
	add_executable(ClipboardLocalizationTests EXCLUDE_FROM_ALL tests/LocalizationTests.cpp src/Localization.cpp)
	add_executable(ClipboardPatternExportTests EXCLUDE_FROM_ALL tests/PatternExportTests.cpp src/AtomicFile.cpp)
	add_executable(ClipboardInputStateTests EXCLUDE_FROM_ALL tests/ClipboardInputStateTests.cpp)
	add_executable(ClipboardComponentRecipeMemoTests EXCLUDE_FROM_ALL tests/ComponentRecipeMemoTests.cpp)
	add_executable(ClipboardComponentInventoryTests EXCLUDE_FROM_ALL tests/ComponentInventoryTests.cpp)
	add_executable(ClipboardReferenceRowsTests EXCLUDE_FROM_ALL tests/ReferenceRowsTests.cpp)
	add_executable(ClipboardSelectionGeometryTests EXCLUDE_FROM_ALL tests/SelectionGeometryTests.cpp)
	add_executable(ClipboardImportCursorPayloadTests EXCLUDE_FROM_ALL tests/ImportCursorPayloadTests.cpp)
	foreach(import_test ClipboardImportJobTests ClipboardConduitConnectionPolicyTests ClipboardLocalizationTests ClipboardPatternExportTests ClipboardInputStateTests ClipboardComponentRecipeMemoTests ClipboardComponentInventoryTests ClipboardReferenceRowsTests ClipboardSelectionGeometryTests ClipboardImportCursorPayloadTests)
		target_compile_features(${import_test} PRIVATE cxx_std_20)
		target_include_directories(${import_test} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
		if(MSVC)
			target_compile_options(${import_test} PRIVATE /sdl /utf-8 /permissive- /Zc:preprocessor /W4 /WX)
			target_link_options(${import_test} PRIVATE /WX)
		endif()
	endforeach()
	add_test(NAME clipboard_import_jobs COMMAND "$<TARGET_FILE:ClipboardImportJobTests>")
	add_test(NAME clipboard_conduit_connection_policy COMMAND "$<TARGET_FILE:ClipboardConduitConnectionPolicyTests>")
	add_test(NAME clipboard_localization COMMAND "$<TARGET_FILE:ClipboardLocalizationTests>"
		"${CMAKE_CURRENT_BINARY_DIR}/test-data/localization")
	add_test(NAME clipboard_pattern_export COMMAND "$<TARGET_FILE:ClipboardPatternExportTests>"
		"${CMAKE_CURRENT_BINARY_DIR}/test-data/pattern-export")
	add_test(NAME clipboard_input_state COMMAND "$<TARGET_FILE:ClipboardInputStateTests>")
	add_test(NAME clipboard_component_recipe_memo COMMAND "$<TARGET_FILE:ClipboardComponentRecipeMemoTests>")
	add_test(NAME clipboard_component_inventory COMMAND "$<TARGET_FILE:ClipboardComponentInventoryTests>")
	add_test(NAME clipboard_reference_rows COMMAND "$<TARGET_FILE:ClipboardReferenceRowsTests>")
	add_test(NAME clipboard_selection_geometry COMMAND "$<TARGET_FILE:ClipboardSelectionGeometryTests>")
	add_test(NAME clipboard_import_cursor_payload COMMAND "$<TARGET_FILE:ClipboardImportCursorPayloadTests>")
	set_tests_properties(clipboard_import_jobs clipboard_conduit_connection_policy clipboard_localization clipboard_pattern_export clipboard_input_state clipboard_component_recipe_memo clipboard_component_inventory clipboard_reference_rows clipboard_selection_geometry clipboard_import_cursor_payload PROPERTIES TIMEOUT 60)

	add_custom_target(clipboard_v240_host_checks
		COMMAND "${CMAKE_CTEST_COMMAND}"
			--test-dir "${CMAKE_CURRENT_BINARY_DIR}"
			-C "$<CONFIG>"
			--output-on-failure
		DEPENDS ClipboardRuntimeDatabasePreflightTests ClipboardLegacyCompatTests ClipboardRuntimeCompatibilityTests ClipboardImportJobTests ClipboardConduitConnectionPolicyTests ClipboardLocalizationTests ClipboardPatternExportTests ClipboardInputStateTests ClipboardComponentRecipeMemoTests ClipboardComponentInventoryTests ClipboardReferenceRowsTests ClipboardSelectionGeometryTests ClipboardImportCursorPayloadTests
		USES_TERMINAL
		VERBATIM
	)
	add_dependencies(stage clipboard_v240_host_checks)
endif()
