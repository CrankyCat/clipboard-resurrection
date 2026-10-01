// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands

#include "EngineSymbols.h"
#include "PapyrusArgument.h"
#include "PapyrusBinding.h"
#include "RuntimeCompatibility.h"
#include "RuntimeDatabasePreflight.h"
#include "RuntimeSymbols.h"
#include "StartupCallbacks.h"
#include "NativeCallBoundaryChecks.h"
#include "PapyrusArrayDispatchTests.h"

#include <Windows.h>
#include <RE/Bethesda/BSLock.h>
#include <RE/Bethesda/BSScript/IComplexType.h>
#include <RE/Bethesda/BSScript/Object.h>
#include <RE/Bethesda/BSScript/Array.h>
#include <RE/Bethesda/BSScript/Struct.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace
{
	constexpr REL::Version kOG{ 1, 10, 163, 0 };
	constexpr REL::Version kNG{ 1, 10, 984, 0 };
	constexpr REL::Version kAE{ 1, 11, 240, 0 };

	// Compile-only ABI/ownership surface checks. Runtime teardown invokes the
	// game string pool even for empty fields and must not run in this host.
	template <class T>
	concept HasEngineHeapOperators = requires {
		static_cast<void (*)(void*)>(&T::operator delete);
		static_cast<void (*)(void*, std::size_t)>(&T::operator delete);
		static_cast<void* (*)(std::size_t)>(&T::operator new);
	};
	static_assert(HasEngineHeapOperators<RE::BSScript::Array>);
	static_assert(HasEngineHeapOperators<RE::BSScript::Struct>);
	static_assert(HasEngineHeapOperators<RE::BSScript::Object>);
	static_assert(sizeof(RE::BSScript::Array) == 0x30);
	static_assert(sizeof(RE::BSScript::Struct) == 0x20);
	static_assert(sizeof(RE::BSScript::Object) == 0x30);

	struct FixtureRVA
	{
		std::uint64_t key;
		std::uint32_t rva;
	};
	// Exact 1.10.163 anchors independently grounded in official F4SE source
	// and preserved executable evidence. See UNIFIED_RUNTIME_COMPATIBILITY.md.
	// These values verify resolver output; they are never executable fallbacks.
	constexpr std::array kOGFixtureRVAs{
		FixtureRVA{ 2204302, 0x004E4420 },
		FixtureRVA{ 2205201, 0x00422180 },
		FixtureRVA{ 2234097, 0x00F0E060 },
		FixtureRVA{ 2194996, 0x001F6890 },
		FixtureRVA{ 2194998, 0x001F6E20 },
		FixtureRVA{ 2195017, 0x001F8830 },
		FixtureRVA{ 2195071, 0x00200B50 },
		FixtureRVA{ 2195088, 0x00201B10 },
		FixtureRVA{ 2195102, 0x00204610 },
		FixtureRVA{ 2195571, 0x001FEE00 },
		FixtureRVA{ 2253499, 0x0140B0E0 },
		FixtureRVA{ 2252785, 0x013D69D0 },
		FixtureRVA{ 2254251, 0x013FE7E0 },
		FixtureRVA{ 2199660, 0x00381030 },
		FixtureRVA{ 4795988, 0x038CCE04 },
		FixtureRVA{ 4797241, 0x058DFDBC },
	};

	[[nodiscard]] constexpr std::optional<std::uint32_t> ExpectedFixtureRVA(
		const REL::Version& runtime, std::string_view scope, std::uint64_t key)
	{
		// Official F4SE GameSettings.cpp, pinned 0.6.23 / 0.7.2 / 0.7.8 /
		// 0.7.9 sources. These verify the two localization globals; no runtime
		// call uses an RVA, and no fixture value is applied to another patch.
		if (scope == "commonlib") {
			if (key == 2704108) {
				if (runtime == kOG) { return 0x05EDB528; }
				if (runtime == kNG) { return 0x03195198; }
				if (runtime == REL::Version{ 1, 11, 221, 0 }) { return 0x0343B038; }
				if (runtime == kAE) { return 0x0344B4B8; }
			}
			if (key == 2703234) {
				if (runtime == kOG) { return 0x05B5BE58; }
				if (runtime == kNG) { return 0x030EF6D0; }
				if (runtime == REL::Version{ 1, 11, 221, 0 }) { return 0x03394A60; }
				if (runtime == kAE) { return 0x0339FAE0; }
			}
		}
		if (scope != "engine") {
			return std::nullopt;
		}
		// Independently traced from Papyrus registration through each executable's
		// SetMotionType functor vtable. Reject a nonzero but incorrect match too.
		if (key == 2253642) {
			if (runtime == kOG) { return 0x0142D160; }
			if (runtime == kNG) { return 0x010E1480; }
			if (runtime == REL::Version{ 1, 11, 221, 0 }) { return 0x011670C0; }
			if (runtime == kAE) { return 0x01167410; }
		}
		if (runtime == kAE) {
			const auto* symbol = Clipboard::EngineAPI::FindRequiredSymbol(key);
			return symbol ? std::optional{ symbol->v240RVA } : std::nullopt;
		}
		if (runtime == kOG) {
			for (const auto& expected : kOGFixtureRVAs) {
				if (expected.key == key) {
					return expected.rva;
				}
			}
		}
		return std::nullopt;
	}

	[[nodiscard]] std::string JsonString(std::string_view a_value)
	{
		std::string result{ "\"" };
		for (const auto value : a_value) {
			const auto byte = static_cast<unsigned char>(value);
			switch (byte) {
			case '"': result += "\\\""; break;
			case '\\': result += "\\\\"; break;
			case '\b': result += "\\b"; break;
			case '\f': result += "\\f"; break;
			case '\n': result += "\\n"; break;
			case '\r': result += "\\r"; break;
			case '\t': result += "\\t"; break;
			default:
				if (byte < 0x20) {
					result += fmt::format("\\u{:04X}", byte);
				} else {
					result += static_cast<char>(byte);
				}
				break;
			}
		}
		result += '"';
		return result;
	}

	[[nodiscard]] std::string PathUTF8(const std::filesystem::path& a_path)
	{
		const auto value = a_path.u8string();
		return { reinterpret_cast<const char*>(value.data()), value.size() };
	}

	std::size_t RunHostChecks()
	{
		std::size_t checks{};
		const auto require = [&checks](bool condition, std::string_view message) {
			++checks;
			if (!condition) {
				throw std::runtime_error(std::string(message));
			}
		};

		using namespace Clipboard;
		NativeCallBoundaryChecks::Run(require);
		PapyrusArrayDispatchTests::Run(require);
		// Model F4SE's stored function pointers and its FreeLibrary on load
		// rejection. Exercise the production lease policy and callback gate,
		// including exceptions after either API has already stored its pointer.
		enum class StartupFailure
		{
			None, BeforePublication, RejectFirst, ThrowFirst, RejectSecond, ThrowSecond, AfterPublication
		};
		struct ModuleLifetime
		{
			std::size_t references{ 1 };  // F4SE's LoadLibrary reference
		};
		struct ModuleLease
		{
			explicit ModuleLease(ModuleLifetime& state) : lifetime(state) { ++lifetime.references; }
			~ModuleLease() { if (ownsReference) { --lifetime.references; } }
			void KeepLoaded() noexcept { ownsReference = false; }
			ModuleLifetime& lifetime;
			bool ownsReference{ true };
		};
		for (const auto failure : { StartupFailure::None, StartupFailure::BeforePublication,
				 StartupFailure::RejectFirst, StartupFailure::ThrowFirst, StartupFailure::RejectSecond,
				 StartupFailure::ThrowSecond, StartupFailure::AfterPublication }) {
			ModuleLifetime module;
			StartupCallbacks::Gate gate;
			std::vector<std::function<void()>> storedCallbacks;
			std::size_t invocations{};
			const auto publish = [&](bool first) {
				if (failure == (first ? StartupFailure::RejectFirst : StartupFailure::RejectSecond)) {
					return false;
				}
				storedCallbacks.emplace_back([&] {
					if (gate.IsEnabled()) { ++invocations; }
				});
				if (failure == (first ? StartupFailure::ThrowFirst : StartupFailure::ThrowSecond)) {
					throw std::runtime_error("registration threw after storing its callback");
				}
				return true;
			};
			bool loaded{};
			try {
				loaded = [&] {
					ModuleLease lease{ module };
					StartupCallbacks::Registration registration{ gate, lease };
					if (failure == StartupFailure::BeforePublication) {
						throw std::runtime_error("initialization failed before registration");
					}
					if (!registration.Attempt([&] { return publish(true); }) ||
						!registration.Attempt([&] { return publish(false); })) {
						return false;
					}
					require(!gate.IsEnabled(), "Stored callbacks must stay inert until the final startup commit");
					if (failure == StartupFailure::AfterPublication) {
						throw std::runtime_error("final load logging failed");
					}
					registration.Commit();
					return true;
				}();
			} catch (const std::runtime_error&) {}
			require(loaded == (failure == StartupFailure::None), "Startup rejection must remain a genuine false result");
			if (!loaded) { --module.references; }  // F4SE unloads its rejected module.
			require(gate.IsEnabled() == loaded, "Only fully committed startup may enable callbacks");
			require(module.references == (loaded || !storedCallbacks.empty() ? 1U : 0U),
				"Only rejected loads with published callbacks retain the extra module reference");
			for (const auto& callback : storedCallbacks) {
				require(module.references != 0, "F4SE's retained callback must not point into an unloaded DLL");
				callback();
			}
			require(invocations == (loaded ? storedCallbacks.size() : 0U),
				"Callbacks left by a rejected load must perform no game operations");
		}

		// Exercise the production handoff with CommonLib's actual intrusive
		// pointer. The fake VM mirrors native binding's temporary ownership,
		// releasing that temporary before returning a genuine accept/reject.
		struct BindingLifetime
		{
			std::size_t destructions{};
			std::uint32_t temporaryReferences{};
			bool callReturned{};
			bool destroyedBeforeReturn{};
		};
		struct BindingFunction final : RE::BSIntrusiveRefCounted
		{
			explicit BindingFunction(BindingLifetime& state) : lifetime(state) {}
			~BindingFunction()
			{
				++lifetime.destructions;
				lifetime.destroyedBeforeReturn = !lifetime.callReturned;
			}
			BindingLifetime& lifetime;
		};
		struct BindingVM
		{
			explicit BindingVM(bool accepts) : accept(accepts) {}
			bool BindNativeMethod(BindingFunction* function)
			{
				auto& lifetime = function->lifetime;
				{
					const RE::BSTSmartPointer<BindingFunction> temporary{ function };
					lifetime.temporaryReferences = function->QRefCount();
					if (accept) {
						retained = temporary;
					}
				}
				lifetime.callReturned = true;
				return accept;
			}
			bool accept;
			RE::BSTSmartPointer<BindingFunction> retained;
		};
		BindingLifetime rejectedLifetime;
		BindingVM rejectingVM{ false };
		require(!PapyrusBinding::Bind(rejectingVM, std::make_unique<BindingFunction>(rejectedLifetime)),
			"A rejected native binding must remain false");
		require(rejectedLifetime.temporaryReferences == 2,
			"The caller must retain a reference alongside the VM binding temporary");
		require(rejectedLifetime.callReturned && !rejectedLifetime.destroyedBeforeReturn,
			"A rejected function must outlive the VM binding call");
		require(rejectedLifetime.destructions == 1 && !rejectingVM.retained,
			"A rejected function must be destroyed exactly once without VM ownership");
		BindingLifetime acceptedLifetime;
		BindingVM acceptingVM{ true };
		require(PapyrusBinding::Bind(acceptingVM, std::make_unique<BindingFunction>(acceptedLifetime)),
			"An accepted native binding must remain true");
		require(acceptedLifetime.callReturned && acceptedLifetime.destructions == 0 &&
			acceptingVM.retained && acceptingVM.retained->QRefCount() == 1,
			"A bound function must remain alive with exactly the VM's retained reference");
		acceptingVM.retained.reset();
		require(acceptedLifetime.destructions == 1 && !acceptedLifetime.destroyedBeforeReturn,
			"Releasing the final VM reference must destroy the accepted function exactly once");

		// Exercise the real Variable copy/type representation without a game VM.
		// The object payload is typed-null; real identifier resolution/dispatch is
		// an in-game gate. Distinct metadata pointers model base/derived classes.
		struct ObjectTypeFixture final : RE::BSScript::IComplexType
		{
			RE::BSScript::TypeInfo::RawType GetRawType() const override
			{
				return RE::BSScript::TypeInfo::RawType::kObject;
			}
		};
		ObjectTypeFixture declaredObjectType;
		ObjectTypeFixture derivedScriptType;
		RE::BSScript::Variable objectArgument;
		*reinterpret_cast<RE::BSScript::TypeInfo*>(std::addressof(objectArgument)) = &derivedScriptType;
		const RE::BSScript::TypeInfo declaredType{ &declaredObjectType };
		require(objectArgument.GetType().data.complexTypeInfo != declaredType.data.complexTypeInfo,
			"Derived script metadata must reproduce the dynamic-call type mismatch");
		require(PapyrusArgument::RestoreDeclaredObjectType(objectArgument, declaredType),
			"A resolved ObjectReference argument must retain its declared type");
		require(objectArgument.GetType().data.complexTypeInfo == declaredType.data.complexTypeInfo,
			"The normalized argument must match the event's exact declared type");
		require(!RE::BSScript::get<RE::BSScript::Object>(objectArgument),
			"Normalizing an object type must preserve its payload");
		RE::BSScript::Variable copiedArgument{ objectArgument };
		RE::BSScript::Variable movedArgument{ std::move(copiedArgument) };
		require(movedArgument.GetType().data.complexTypeInfo == declaredType.data.complexTypeInfo,
			"Variable copies/moves must retain the declared type before dispatch");
		RE::BSScript::Variable scalarArgument;
		scalarArgument = 42;
		require(!PapyrusArgument::RestoreDeclaredObjectType(scalarArgument, declaredType) &&
			RE::BSScript::get<std::int32_t>(scalarArgument) == 42,
			"A scalar must not be disguised as an ObjectReference");
		require(!PapyrusArgument::RestoreDeclaredObjectType(objectArgument,
			RE::BSScript::TypeInfo::RawType::kInt) &&
			objectArgument.GetType().data.complexTypeInfo == declaredType.data.complexTypeInfo,
			"A non-object declaration must not change the object argument");
		RE::BSScript::Variable noneArgument;
		require(!PapyrusArgument::RestoreDeclaredObjectType(noneArgument, declaredType) &&
			noneArgument.is<std::nullptr_t>(), "None keeps its existing dispatch semantics");

		require(RuntimeCompatibility::SupportsDeclaredLayout(kOG), "OG must reach capability resolution");
		require(RuntimeCompatibility::SupportsDeclaredLayout(kNG), "NG must reach capability resolution");
		require(RuntimeCompatibility::SupportsDeclaredLayout(kAE), "AE must reach capability resolution");
		require(RuntimeCompatibility::SupportsDeclaredLayout(REL::Version{ 1, 11, 241, 0 }),
			"An unlisted AE patch must not be rejected before capability resolution");
		require(RuntimeCompatibility::FamilyName(kOG) == "OG", "OG fixture family");
		require(RuntimeCompatibility::FamilyName(kNG) == "NG", "NG fixture family");
		require(RuntimeCompatibility::FamilyName(kAE) == "AE", "AE fixture family");
		require(RuntimeCompatibility::FamilyName(REL::Version{ 1, 10, 980, 0 }) == "NG", "NG lower family boundary");
		require(RuntimeCompatibility::FamilyName(REL::Version{ 1, 11, 0, 0 }) == "AE", "AE lower family boundary");
		require(RuntimeCompatibility::kStructureFlags == 0x6,
			"Metadata must declare both NG and AE layouts without NoStructs");
		REL::IDResolveResult provenanceResult{};
		provenanceResult.id = 608512;
		provenanceResult.rva = 0x00204610;
		provenanceResult.finalRva = 0x00204610;
		provenanceResult.status = REL::IDResolveStatus::kResolvedLegacy;
		const auto acceptedLegacy = RuntimeCompatibility::CheckResolutionProvenance(provenanceResult, kOG);
		require(acceptedLegacy && acceptedLegacy.rva == provenanceResult.rva,
			"The embedded OG table is grounded for its exact 1.10.163 fixture");
		const REL::Version earlierOG{ 1, 10, 120, 0 };
		const auto rejectedLegacy = RuntimeCompatibility::CheckResolutionProvenance(provenanceResult, earlierOG);
		require(!rejectedLegacy && !rejectedLegacy.rva && !rejectedLegacy.finalRva &&
			rejectedLegacy.status == REL::IDResolveStatus::kUnresolved,
			"Nonzero embedded 1.10.163 addresses must be rejected on other OG patches");
		provenanceResult.status = REL::IDResolveStatus::kResolvedKnownRVA;
		const auto acceptedKnown = RuntimeCompatibility::CheckResolutionProvenance(provenanceResult, earlierOG);
		require(acceptedKnown && acceptedKnown.rva == provenanceResult.rva && acceptedKnown.status == provenanceResult.status,
			"Runtime-specific known addresses remain eligible on other OG patches");
		provenanceResult.status = REL::IDResolveStatus::kResolvedPattern;
		const auto acceptedPattern = RuntimeCompatibility::CheckResolutionProvenance(provenanceResult, earlierOG);
		require(acceptedPattern && acceptedPattern.rva == provenanceResult.rva && acceptedPattern.status == provenanceResult.status,
			"The provenance guard must preserve successful pattern resolution");

		// The logical key is independent of the selected runtime's ID. Unknown
		// keys must remain a checked failure, never an accidental AE fallback.
		require(EngineAPI::FindRequiredSymbol(0) == nullptr, "Zero is not an engine symbol key");
		require(EngineAPI::FindRequiredSymbol(std::numeric_limits<std::uint64_t>::max()) == nullptr,
			"Invalid logical key must not resolve to an arbitrary entry");
		require(EngineAPI::kRequiredIDs.size() == 41, "The reviewed engine manifest must retain all 40 original entries plus synchronous motion");
		std::set<std::uint64_t> keys;
		for (const auto& symbol : EngineAPI::kRequiredIDs) {
			require(keys.insert(symbol.key).second, "Duplicate logical engine key");
			require(!symbol.name.empty() && symbol.v240RVA != 0, "Engine symbol requires name and fixture provenance");
			require(EngineAPI::FindRequiredSymbol(symbol.key) == std::addressof(symbol),
				"Logical-key lookup must return the exact manifest row");
			require(symbol.id.id(kOG) != 0 && symbol.id.id(kNG) != 0 && symbol.id.id(kAE) != 0,
				"Engine tuple must select a nonzero ID in every family");
		}

		const auto checkTuple = [&require](std::uint64_t key, std::uint64_t og, std::uint64_t ng, std::uint64_t ae) {
			const auto* symbol = EngineAPI::FindRequiredSymbol(key);
			require(symbol != nullptr, "Expected reviewed symbol missing");
			require(symbol->id.id(kOG) == og, "OG symbol selection differs from reviewed executable/database evidence");
			require(symbol->id.id(kNG) == ng, "NG symbol selection differs from reviewed executable/database evidence");
			require(symbol->id.id(kAE) == ae, "AE symbol selection differs from reviewed executable/database evidence");
		};
		checkTuple(4795988, 888641, 2688724, 4795988);  // invalid handle: all families differ
		checkTuple(4797241, 737927, 2689952, 4797241);  // current workshop: all families differ
		checkTuple(2195571, 1069718, 2195571, 2195571);  // same private five-argument helper
		checkTuple(2253499, 984532, 2253499, 2253499);  // PlaceAtMe
		checkTuple(2199660, 958030, 2199660, 2199660);  // XLRL handle-map getter
		require(EngineAPI::FindRequiredSymbol(2195102)->requireOGLegacy,
			"OG terminal links must bypass the proven false canonical pattern");
		require(kOGFixtureRVAs.size() == 16, "Retain all independently grounded OG fixture anchors");
		std::set<std::uint64_t> fixtureKeys;
		for (const auto& expected : kOGFixtureRVAs) {
			require(fixtureKeys.insert(expected.key).second, "Duplicate OG fixture anchor key");
			require(EngineAPI::FindRequiredSymbol(expected.key) != nullptr && expected.rva != 0,
				"OG fixture anchor requires a manifest entry and nonzero RVA");
		}
		require(ExpectedFixtureRVA(kOG, "engine", 2195102) == 0x00204610,
			"OG terminal-link fixture must identify the actual one-argument helper");
		require(!ExpectedFixtureRVA(kNG, "engine", 2195102), "Do not apply OG or AE fixture RVAs to NG");
		require(!ExpectedFixtureRVA(REL::Version{ 1, 11, 241, 0 }, "engine", 2195102),
			"Exact fixture assertions must not become a runtime patch whitelist");

		require(!RuntimeSymbols::kCommonLibSymbols.empty(), "Reachable CommonLib manifest must be present");
		std::set<std::string_view> names;
		for (const auto& symbol : RuntimeSymbols::kCommonLibSymbols) {
			require(!symbol.name.empty() && !symbol.provenance.empty(), "CommonLib row requires name and pinned source provenance");
			require(names.insert(symbol.name).second, "Duplicate CommonLib logical symbol name");
			require(symbol.id.id(kOG) != 0 && symbol.id.id(kNG) != 0 && symbol.id.id(kAE) != 0,
				"CommonLib tuple must select a nonzero ID in every family");
		}
		return checks;
	}

	int LoadPluginInitializers(const std::filesystem::path& a_path)
	{
		if (!a_path.is_absolute() || !std::filesystem::is_regular_file(a_path)) {
			throw std::runtime_error("--load-plugin requires an absolute existing plugin DLL path");
		}
		if (!std::filesystem::is_empty(std::filesystem::current_path()) || ::GetModuleHandleW(L"Fallout4.exe")) {
			throw std::runtime_error("DLL initializer smoke check requires an empty CWD and no Fallout4.exe module");
		}
		if (!::SetEnvironmentVariableW(L"F4SE_RUNTIME", L"Fallout4.exe")) {
			throw std::runtime_error("Could not select the deliberately absent game module for the smoke check");
		}
		const auto path = std::filesystem::canonical(a_path);
		// This mode intentionally executes this built DLL's CRT/DllMain only.
		// It must succeed before F4SEPlugin_Load, Runtime Database or game access.
		const auto plugin = ::LoadLibraryW(path.c_str());
		if (!plugin) {
			throw std::runtime_error(fmt::format("Loading plugin initializers failed: {}", ::GetLastError()));
		}
		const bool loadExport = ::GetProcAddress(plugin, "F4SEPlugin_Load") != nullptr;
		const bool queryExport = ::GetProcAddress(plugin, "F4SEPlugin_Query") != nullptr;
		const auto* metadata = reinterpret_cast<const F4SE::PluginVersionData*>(
			::GetProcAddress(plugin, "F4SEPlugin_Version"));
		const bool versionExport = metadata != nullptr;
		const bool validMetadata = metadata && metadata->dataVersion == F4SE::PluginVersionData::kVersion &&
			metadata->addressIndependence == F4SE::PluginVersionData::kAddressIndependence_Signatures &&
			metadata->structureIndependence == Clipboard::RuntimeCompatibility::kStructureFlags &&
			std::all_of(std::begin(metadata->compatibleVersions), std::end(metadata->compatibleVersions),
				[](std::uint32_t version) { return version == 0; });
		if (!::FreeLibrary(plugin)) {
			throw std::runtime_error(fmt::format("Unloading plugin initializers failed: {}", ::GetLastError()));
		}
		const bool passed = loadExport && queryExport && versionExport && validMetadata;
		std::cout << "{\"mode\":\"dll-initializer-smoke\",\"plugin\":" << JsonString(PathUTF8(path))
			<< ",\"passed\":" << (passed ? "true" : "false")
			<< ",\"loadExport\":" << (loadExport ? "true" : "false")
			<< ",\"queryExport\":" << (queryExport ? "true" : "false")
			<< ",\"versionExport\":" << (versionExport ? "true" : "false")
			<< ",\"validMetadata\":" << (validMetadata ? "true" : "false")
			<< ",\"f4sePluginLoadCalled\":false,\"engineCodeExecuted\":false,\"unloaded\":true}\n";
		return passed ? 0 : 2;
	}

	// Only inspect the resolved address. Calling either result would enter game
	// code, which is deliberately excluded from this offline benchmark.
	__declspec(noinline) std::uintptr_t RepeatedReadLockAddress()
	{
		using LockRead = decltype(&RE::BSReadWriteLock::lock_read);
		const REL::Relocation<LockRead> lockRead{ REL::ID(1573164, 2267897) };
		return lockRead.address();
	}

	__declspec(noinline) std::uintptr_t CachedReadLockAddress()
	{
		using LockRead = decltype(&RE::BSReadWriteLock::lock_read);
		static const REL::Relocation<LockRead> lockRead{ REL::ID(1573164, 2267897) };
		return lockRead.address();
	}

	struct ReadLockResolutionMeasurement
	{
		double elapsedMs{};
		std::uintptr_t checksum{};
		bool addressesMatch{ true };
	};

	ReadLockResolutionMeasurement MeasureReadLockResolution(
		std::uintptr_t (*accessor)(), std::uintptr_t expectedAddress, std::size_t iterations)
	{
		// Match the observable loop work in both cases. A volatile function
		// pointer prevents hoisting the cached accessor out of the timed loop.
		std::uintptr_t (*volatile getAddress)() = accessor;
		volatile std::uintptr_t checksum{};
		bool addressesMatch = true;
		const auto started = std::chrono::steady_clock::now();
		for (std::size_t i = 0; i < iterations; ++i) {
			const auto address = getAddress();
			checksum = checksum + address;
			addressesMatch = (address == expectedAddress) && addressesMatch;
		}
		const auto elapsedMs = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - started).count();
		return { elapsedMs, checksum, addressesMatch };
	}

	int InspectExecutable(const std::filesystem::path& a_path, bool benchmarkReadLockResolution = false)
	{
		if (!a_path.is_absolute() || !std::filesystem::is_regular_file(a_path)) {
			throw std::runtime_error("--inspect requires an absolute existing executable path");
		}
		const auto path = std::filesystem::canonical(a_path);
		const auto version = REL::get_file_version(path.native().c_str());
		if (!version) {
			throw std::runtime_error("The executable has no ProductVersion readable by the pinned Module implementation");
		}

		// This is the same structural validation/held mapping as plugin load.
		// Root supplies a database copy under this process's controlled CWD.
		const auto database = Clipboard::RuntimeDatabasePreflight::ValidateAndHoldRuntimeCandidates(
			std::filesystem::current_path() / "Data/F4SE/Plugins", version->string());
		if (!database) {
			throw std::runtime_error("Runtime Database preflight failed: " + database.error);
		}

		// DONT_RESOLVE_DLL_REFERENCES maps the PE without executing its entry
		// point or resolving/importing dependencies. No game code is called.
		// Keep the mapping until process exit: REL singletons retain its base.
		const auto mapped = ::LoadLibraryExW(path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
		if (!mapped) {
			throw std::runtime_error(fmt::format("LoadLibraryExW without execution failed: {}", ::GetLastError()));
		}
		if (!::SetEnvironmentVariableW(L"F4SE_RUNTIME", path.c_str())) {
			throw std::runtime_error(fmt::format("Setting process-local F4SE_RUNTIME failed: {}", ::GetLastError()));
		}
		if (::GetModuleHandleW(path.c_str()) != mapped) {
			throw std::runtime_error("Windows did not expose the mapped executable to GetModuleHandleW; pinned Module cannot inspect it");
		}
		const auto& module = REL::Module::get();
		if (module.base() != reinterpret_cast<std::uintptr_t>(mapped) || module.version() != *version) {
			throw std::runtime_error("Pinned Module selected a different image/version from the requested fixture");
		}

		if (benchmarkReadLockResolution) {
			// Match the parent count in the recorded manual-selection capture.
			// Warm both the Runtime Database cache and function-local static;
			// measure repeated lookup overhead, not first-resolution startup.
			constexpr std::size_t iterations = 1058318;
			constexpr std::size_t warmupIterations = 256;
			constexpr REL::ID lockID{ 1573164, 2267897 };
			const auto address = RepeatedReadLockAddress();
			if (address <= module.base() || address - module.base() >= module.image_size() ||
				CachedReadLockAddress() != address) {
				throw std::runtime_error("Read-lock resolvers did not agree on a valid in-module address");
			}
			const auto repeatedWarmup = MeasureReadLockResolution(
				RepeatedReadLockAddress, address, warmupIterations);
			const auto cachedWarmup = MeasureReadLockResolution(
				CachedReadLockAddress, address, warmupIterations);
			const auto repeated = MeasureReadLockResolution(RepeatedReadLockAddress, address, iterations);
			const auto cached = MeasureReadLockResolution(CachedReadLockAddress, address, iterations);
			const auto expectedChecksum = address * static_cast<std::uintptr_t>(iterations);
			const bool addressesMatch = repeatedWarmup.addressesMatch && cachedWarmup.addressesMatch &&
				repeated.addressesMatch && cached.addressesMatch;
			const bool passed = addressesMatch && repeated.checksum == expectedChecksum &&
				cached.checksum == expectedChecksum;
			std::cout << "{\n  \"mode\":\"read-lock-resolution-benchmark\",\n  \"executable\":" << JsonString(PathUTF8(path))
				<< ",\n  \"runtime\":" << JsonString(module.version().string())
				<< ",\n  \"family\":" << JsonString(Clipboard::RuntimeCompatibility::FamilyName(module.version()))
				<< ",\n  \"database\":" << JsonString(PathUTF8(database.databasePath))
				<< ",\n  \"databaseRecords\":" << database.recordCount
				<< ",\n  \"loadFlags\":\"DONT_RESOLVE_DLL_REFERENCES\",\n  \"engineCodeExecuted\":false"
				<< ",\n  \"measurement\":\"Warmed address resolution only; excludes engine locking, VM queries and game selection\""
				<< ",\n  \"iterations\":" << iterations << ",\n  \"warmupIterations\":" << warmupIterations
				<< ",\n  \"ogID\":" << lockID.og_id() << ",\n  \"ngID\":" << lockID.ng_id()
				<< ",\n  \"aeID\":" << lockID.ae_id() << ",\n  \"selectedID\":" << lockID.id(module.version())
				<< ",\n  \"rva\":" << address - module.base()
				<< ",\n  \"repeatedMs\":" << repeated.elapsedMs << ",\n  \"cachedMs\":" << cached.elapsedMs
				<< ",\n  \"repeatedChecksum\":" << repeated.checksum << ",\n  \"cachedChecksum\":" << cached.checksum
				<< ",\n  \"addressesMatch\":" << (addressesMatch ? "true" : "false")
				<< ",\n  \"passed\":" << (passed ? "true" : "false") << "\n}\n";
			return passed ? 0 : 2;
		}

		std::ostringstream entries;
		std::size_t total{};
		std::size_t resolved{};
		std::size_t failed{};
		std::size_t expectedChecks{};
		std::size_t expectedMismatches{};
		const auto inspect = [&](std::string_view scope, std::uint64_t key, const REL::ID& id,
			std::string_view name, const REL::IDResolveResult& result) {
			const auto expected = ExpectedFixtureRVA(module.version(), scope, key);
			const bool matchesExpected = !expected || (result.rva && *result.rva == *expected);
			if (expected) {
				++expectedChecks;
				expectedMismatches += matchesExpected ? 0 : 1;
			}
			const bool valid = result && result.rva && *result.rva != 0 && *result.rva < module.image_size() && matchesExpected;
			if (total++ != 0) {
				entries << ',';
			}
			entries << "\n    {\"scope\":" << JsonString(scope)
				<< ",\"key\":" << key << ",\"name\":" << JsonString(name)
				<< ",\"ogID\":" << id.og_id() << ",\"ngID\":" << id.ng_id() << ",\"aeID\":" << id.ae_id()
				<< ",\"selectedID\":" << id.id(module.version()) << ",\"resolvedID\":" << result.id
				<< ",\"status\":" << JsonString(REL::id_resolve_status_text(result.status))
				<< ",\"rva\":";
			if (result.rva) {
				entries << *result.rva;
			} else {
				entries << "null";
			}
			entries << ",\"expectedRva\":";
			if (expected) {
				entries << *expected;
			} else {
				entries << "null";
			}
			entries << ",\"matchesExpected\":" << (expected ? (matchesExpected ? "true" : "false") : "null")
				<< ",\"valid\":" << (valid ? "true" : "false") << '}';
			valid ? ++resolved : ++failed;
		};
		for (const auto& symbol : Clipboard::EngineAPI::kRequiredIDs) {
			// Exercise the plugin's shared resolution policy, including the
			// grounded OG terminal-link exception, without calling its result.
			inspect("engine", symbol.key, symbol.id, symbol.name, Clipboard::EngineAPI::ResolveRequiredSymbol(symbol));
		}
		for (const auto& symbol : Clipboard::RuntimeSymbols::kCommonLibSymbols) {
			inspect("commonlib", symbol.id.ae_id(), symbol.id, symbol.name, Clipboard::RuntimeSymbols::ResolveSymbol(symbol));
		}

		std::cout << "{\n  \"mode\":\"offline-mapped-pe\",\n  \"executable\":" << JsonString(PathUTF8(path))
			<< ",\n  \"runtime\":" << JsonString(module.version().string())
			<< ",\n  \"family\":" << JsonString(Clipboard::RuntimeCompatibility::FamilyName(module.version()))
			<< ",\n  \"database\":" << JsonString(PathUTF8(database.databasePath))
			<< ",\n  \"databaseRecords\":" << database.recordCount
			<< ",\n  \"loadFlags\":\"DONT_RESOLVE_DLL_REFERENCES\",\n  \"engineCodeExecuted\":false"
			<< ",\n  \"engineManifestCount\":" << Clipboard::EngineAPI::kRequiredIDs.size()
			<< ",\n  \"commonLibManifestCount\":" << Clipboard::RuntimeSymbols::kCommonLibSymbols.size()
			<< ",\n  \"expectedChecks\":" << expectedChecks << ",\n  \"expectedMismatches\":" << expectedMismatches
			<< ",\n  \"entries\":[" << entries.str() << "\n  ],\n  \"total\":" << total
			<< ",\n  \"resolved\":" << resolved << ",\n  \"failed\":" << failed << "\n}\n";
		return failed == 0 ? 0 : 2;
	}
}

int wmain(int argc, wchar_t** argv)
{
	// Keep machine-readable stdout separate from the dependency's diagnostics.
	spdlog::set_level(spdlog::level::off);
	::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	try {
		const auto checks = RunHostChecks();
		if (argc == 1) {
			std::cout << "{\"mode\":\"host\",\"passed\":true,\"checks\":" << checks << "}\n";
			return 0;
		}
		if (argc == 3 && std::wstring_view(argv[1]) == L"--inspect") {
			return InspectExecutable(argv[2]);
		}
		if (argc == 3 && std::wstring_view(argv[1]) == L"--benchmark-read-lock-resolution") {
			return InspectExecutable(argv[2], true);
		}
		if (argc == 3 && std::wstring_view(argv[1]) == L"--load-plugin") {
			return LoadPluginInitializers(argv[2]);
		}
		throw std::runtime_error("Usage: ClipboardRuntimeCompatibilityTests [--inspect <absolute executable> | --benchmark-read-lock-resolution <absolute executable> | --load-plugin <absolute plugin DLL>]");
	} catch (const std::exception& error) {
		std::cout << "{\"passed\":false,\"error\":" << JsonString(error.what()) << "}\n";
		return 1;
	}
}
