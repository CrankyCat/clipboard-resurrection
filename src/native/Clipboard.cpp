// SPDX-License-Identifier: GPL-3.0-or-later
// Clipboard - Fallout 4 object selection, copying, and placement.
// Copyright (C) 2018 to 2026 Everett C Sands
// Maintained CommonLibF4RD port; modified September 2026.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; see LICENSE.txt in the project root.
// If not, see <https://www.gnu.org/licenses/>.
#include "PCH.h"
#include "LoggingPolicy.h"
#include "LoggingSink.h"

#include "EngineAPI.h"
#include "EngineTaskDispatch.h"
#include "ClipboardInputService.h"
#include "ClipboardInputUI.h"
#include "AttachedScriptCandidates.h"
#include "ConduitConnections.h"
#include "ComponentRecipeMemo.h"
#include "ComponentInventory.h"
#include "ImportAnimation.h"
#include "ImportTransform.h"
#include "ImportProgress.h"
#include "ImportDuplicates.h"
#include "LatentBridge.h"
#include "LocalizationRuntime.h"
#include "LegacyCompat.h"
#include "MasterPluginBlacklist.h"
#include "NativeCallBoundary.h"
#include "OptionalFiltering.h"
#include "OptionalFilteringPolicy.h"
#include "PapyrusBinding.h"
#include "PatternWireRows.h"
#include "AtomicFile.h"
#include "PatternExportValidation.h"
#include "PatternNumbers.h"
#include "ExistingPowerEndpoints.h"
#include "FirstReferenceRows.h"
#include "PatternSourceMovement.h"
#include "RuntimeDatabasePreflight.h"
#include "RuntimeCompatibility.h"
#include "RuntimeSymbols.h"
#include "ScrapTargets.h"
#include "SelectionGeometry.h"
#include "ScaleConstraints.h"
#include "SettingsNumbers.h"
#include "StartupCallbacks.h"
#include "Version.h"
#include "WorkshopCellScope.h"
#include "WorkshopCallbackSpacing.h"
#include "WorkshopCallbackLimit.h"
#include "WideSelectionDistance.h"

#include <bit>

// Writable CommonLibF4RD port. The historical 1.11.221/F4SE 0.7.8 source is
// preserved separately under legacy/v221 and Original Project Files.
constexpr char kPapyrusClassName[] = { "ClipboardExtension" };
const UInt32 pluginVersion =
	(static_cast<UInt32>(Version::MAJOR) << 24) |
	(static_cast<UInt32>(Version::MINOR) << 16) |
	(static_cast<UInt32>(Version::PATCH) << 4) |
	static_cast<UInt32>(Version::TWEAK);
const std::string pluginVersionString{ Version::NAME };
const long double PI = acos(-1.0L);
const int TOOL_OFFSET = 64;
const UInt32 BOTTLECAP_FORM_ID = 0xF;
const std::string MANNEQUIN_RACE_EDITOR_ID = "DLC05ArmorRackRace";

struct TemplateItem {
	std::string plugin;
	UInt32	formId;
};

std::ofstream logOutStream;
BGSKeyword* actorTypeTurretKeyword;
BGSKeyword* actorTypeCreatureKeyword;
BGSKeyword* workshopItemKeyword;
BGSKeyword* workshopKeyword;
BGSKeyword* clipboardSelectedKeyword;
auto& enableLogging = Clipboard::Logging::enabled;
bool enableVerboseImportLogging = false;
thread_local std::uint32_t importLogDepth = 0;

class ScopedImportLog
{
public:
	ScopedImportLog() { ++importLogDepth; }
	~ScopedImportLog() { --importLogDepth; }
	ScopedImportLog(const ScopedImportLog&) = delete;
	ScopedImportLog& operator=(const ScopedImportLog&) = delete;
};
bool enableAnimalSelection = false;
bool allowNormallyFilteredObjectsToBeSelectedAndExported = false;
bool allowAllObjectsToBeImported = false;
std::vector<TemplateItem> blackListedForms;
std::vector<std::string> blackListedPlugins;

namespace
{
	Clipboard::StartupCallbacks::Gate startupCallbackGate;
}

void ResetLegacyCleanupSnapshot(std::int32_t token = 0);


// trim from start (in place)
static inline void ltrim(std::string& s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
		return !std::isspace(static_cast<unsigned char>(ch));
		}));
}

// trim from end (in place)
static inline void rtrim(std::string& s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
		return !std::isspace(static_cast<unsigned char>(ch));
		}).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string& s) {
	ltrim(s);
	rtrim(s);
}

// equals, ignore case
static inline bool iequals(const std::string& a, const std::string& b) {
	if (a.size() != b.size()) {
		return false;
	}
	return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char a, char b) {
		return std::tolower(static_cast<unsigned char>(a)) ==
			std::tolower(static_cast<unsigned char>(b));
		});
}

// starts with, ignore case
static inline bool istarts_with(const std::string& value, const std::string& prefix) {
	if (value.size() < prefix.size()) {
		return false;
	}
	return std::equal(prefix.begin(), prefix.end(), value.begin(), [](char a, char b) {
		return std::tolower(static_cast<unsigned char>(a)) ==
			std::tolower(static_cast<unsigned char>(b));
		});
}

// safely convert to float, 0 on error
static float ToFloat(const std::string floatString) {
	return Clipboard::PatternNumbers::Parse<float>(floatString);
}

// safely convert to UInt32, 0 on error
static UInt32 ToInt(const std::string intString) {
	try {
		return std::stoi(intString);
	}
	catch (...) {
		return 0;
	}
}

// safely convert to UInt64, 0 on error
static UInt64 ToInt64(const std::string intString) {
	try {
		return std::stoll(intString);
	}
	catch (...) {
		return 0;
	}
}

// Preserve each source number rather than rounding every field to six places.
template <class Number>
static std::string ToString(Number val) {
	return Clipboard::PatternNumbers::Format(val);
}

// Get current date/time, format is YYYY-MM-DD.HH:mm:ss
const std::string GetCurrentDateTimeString() {
	const time_t now = time(0);
	struct tm tstruct;
	localtime_s(&tstruct,&now);
	char buf[80];
	strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);
	return buf;
}

// Return the Fallout 4 installation directory with the historical trailing
// separator expected by the settings and pattern-path code.
static std::string GetRuntimeDirectory()
{
	std::array<wchar_t, 32768> modulePath{};
	const auto length = GetModuleFileNameW(
		nullptr,
		modulePath.data(),
		static_cast<DWORD>(modulePath.size()));
	if (length == 0 || length >= modulePath.size()) {
		return {};
	}
	const auto runtime = std::filesystem::path{ modulePath.data() }.parent_path();
	if (runtime.empty()) {
		return {};
	}
	auto path = runtime.string();
	if (!path.empty() && path.back() != '\\' && path.back() != '/') {
		path.push_back('\\');
	}
	return path;
}

// Path to log file.
const std::string GetLogFilePath() { // WMK
	const std::string runtimePath = GetRuntimeDirectory();
	if (!runtimePath.empty()) {
		return runtimePath + "Data\\F4SE\\plugins\\clipboard.log";
	}
	return {};
}

// Path to MCM settings file.
const std::string GetSettingFilePath() { // WMK
	static std::string	runtimePath = GetRuntimeDirectory();
	if (!runtimePath.empty()) {
		return runtimePath + "Data\\MCM\\Settings\\clipboard.ini";
	}
	return {};
}

// Path to default settings file.
const std::string GetDefaultSettingFilePath() { // WMK
	std::string	runtimePath = GetRuntimeDirectory();
	if (!runtimePath.empty()) {
		return runtimePath + "Data\\MCM\\Config\\Clipboard\\settings.ini";
	}
	return {};
}

// Join together the given vector of string using the given delimiter between them.
std::string Join(const std::vector<std::string>& v, const char del) {
	std::string rtnString;

	for (std::vector<std::string>::const_iterator p = v.begin(); p != v.end(); ++p) {
		rtnString += *p;
		if (p != v.end() - 1)
			rtnString += del;
	}

	return rtnString;
}

// Split the given string into a vector of string using the given delimiter.
std::vector<std::string> split(const std::string str, const char del) {
	std::stringstream stream(str);
	std::vector<std::string> result;

	while (stream.good()) {
		std::string substr;
		std::getline(stream, substr, del);
		result.push_back(substr.c_str());
	}

	return result;
}

// Legacy diagnostic text retains its severity even with diagnostics disabled.
void AppendToLog(std::string text) {
    if (istarts_with(text, "Error") || istarts_with(text, "Failed")) {
        logger::error("{}", text);
    } else if (istarts_with(text, "WARNING") || istarts_with(text, "Missing form") ||
        istarts_with(text, "Invalid") || istarts_with(text, "Mod Missing")) {
        logger::warn("{}", text);
    } else {
        CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, logger::info("{}", text));
    }
}

void ReportScriptIssue(StaticFunctionTag*, BSFixedString message, bool isError) {
    if (isError) { logger::error("{}", message.c_str()); }
    else { logger::warn("{}", message.c_str()); }
}


// Keyword arrays can exceed the historical 255-character INI buffer. Never
// consume a truncated list: the last fragment could resolve a different form.
static std::string ReadIniValue(const char* section, const char* key,
	const char* fallback, const std::string& path)
{
	std::vector<char> buffer(256);
	for (;;) {
		const auto copied = GetPrivateProfileStringA(section, key, fallback,
			buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
		if (copied < buffer.size() - 1) {
			return std::string(buffer.data(), copied);
		}
		if (buffer.size() >= 65536) {
			logger::error("INI [{}] {} exceeds 65534 characters; ignoring the entire value", section, key);
			return {};
		}
		buffer.resize(buffer.size() * 2);
	}
}

// Retreive the setting value from (In ascending order or priority)
//		1. The privded default value
//		2. The default settings file
//		3. The MCM settings file
std::string GetSettingValue(const char* section, const char* key, const char* defaultValue) {

	const std::string& defaultConfigPath = GetDefaultSettingFilePath();
	CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog(("Loading [" + std::string(section) + "].[" + std::string(key) + "] Default: " + std::string(defaultValue))));

	std::string defaultFileValue;
	if (!defaultConfigPath.empty()) {
		defaultFileValue = ReadIniValue(section, key, defaultValue, defaultConfigPath);
		CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Clipboard Setting [" + std::string(section) + "].[" + std::string(key) + "] Value: " + defaultFileValue));
	}
	else {
		defaultFileValue = defaultValue;
		CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Clipboard Setting Not Found"));
	}

	const std::string& configPath = GetSettingFilePath();
	std::string result;
	if (!configPath.empty()) {
		result = ReadIniValue(section, key, defaultFileValue.c_str(), configPath);
		CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("MCM Setting [" + std::string(section) + "].[" + std::string(key) + "] Value: " + result));
	}
	else {
		result = defaultFileValue;
		CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("MCM Setting Not Found"));
	}

	CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found [" + std::string(section) + "].[" + std::string(key) + "] Value: " + std::string(result)));
	return result;
}

// Retreieve a string setting.
BSFixedString GetSettingValueString(StaticFunctionTag* base, const BSFixedString section, const BSFixedString key, const BSFixedString defaultValue) {
	return BSFixedString(GetSettingValue(section.c_str(), key.c_str(), defaultValue.c_str()).c_str());
}

// Whole templates are localized before presentation. Values such as pattern,
// character and plugin names are literal arguments, never translation keys.
BSFixedString GetText(StaticFunctionTag*, const BSFixedString key,
	const BSFixedString arg0, const BSFixedString arg1, const BSFixedString arg2,
	const BSFixedString arg3, const BSFixedString arg4, const BSFixedString arg5)
{
	try {
		const Clipboard::Localization::Arguments arguments{
			arg0.c_str(), arg1.c_str(), arg2.c_str(), arg3.c_str(), arg4.c_str(), arg5.c_str()
		};
		const auto text = Clipboard::Localization::GetRuntimeText(key.c_str(), arguments);
		return BSFixedString(text.c_str());
	} catch (const std::exception& error) {
		logger::error("Clipboard localization lookup failed: {}", error.what());
		return BSFixedString(Clipboard::Localization::kUnavailableText.data());
	}
}

// Retreieve an int setting.
UInt32 GetSettingValueInt(StaticFunctionTag* base, const BSFixedString section, const BSFixedString key, const UInt32 defaultValue) {
	std::string resultString = GetSettingValue(section.c_str(), key.c_str(), std::to_string(std::bit_cast<std::int32_t>(defaultValue)).c_str());
	return Clipboard::SettingsNumbers::Int(resultString, defaultValue);
}

// Retreieve a float setting.
float GetSettingValueFloat(StaticFunctionTag* base, const BSFixedString section, const BSFixedString key, const float defaultValue) {
	std::string resultString = GetSettingValue(section.c_str(), key.c_str(), std::to_string(defaultValue).c_str());
	return Clipboard::SettingsNumbers::Float(resultString, defaultValue);
}

// Read the logging switch without logging the setting lookup itself. This is
// safe before F4SE initialization: only the executable path and INI APIs run.
void RefreshLoggingSetting()
{
	Clipboard::Logging::SetEnabled(false);
	const auto defaults = ReadIniValue("Debug", "bEnableLogging", "0", GetDefaultSettingFilePath());
	const auto value = ReadIniValue("Debug", "bEnableLogging", defaults.c_str(), GetSettingFilePath());
	Clipboard::Logging::SetEnabled(iequals(value, "true") || iequals(value, "1"));
}

// Retreieve a boolean setting.
bool GetSettingValueBool(StaticFunctionTag* base, const BSFixedString section, const BSFixedString key, const bool defaultValue) {
	if (iequals(section.c_str(), "Debug") && iequals(key.c_str(), "bEnableLogging")) {
		RefreshLoggingSetting();
		return Clipboard::Logging::Enabled();
	}
	std::string resultString = GetSettingValue(section.c_str(), key.c_str(), (defaultValue ? "true" : "false"));
	return iequals(resultString, "true") || iequals(resultString, "1");
}

bool HideConditionBoyDuringInput()
{
	return GetSettingValueBool(nullptr, "Dialogs", "bHideConditionBoyDuringInput", true);
}

// Snapshot once per workshop initialization job. Only explicit On enables
// throttling; missing or malformed settings retain the packaged Off default.
bool UseWorkshopThrottling()
{
	std::string value = GetSettingValue("Import", "bUseThrottling", "0");
	trim(value);
	return iequals(value, "true") || value == "1";
}

// Opt-in diagnostic, read once at initialization start. Logging is also
// required at each observation; missing/malformed settings stay disabled.
bool EnableWorkshopWaitSnapshots()
{
	std::string value = GetSettingValue("ImportDiagnostics", "bWorkshopWaitSnapshots", "0");
	trim(value);
	return iequals(value, "true") || value == "1";
}

// One five-second observation, scoped to On and single/explicit limit at 0ms.
// Missing or malformed values leave the existing 30/60/90-second schedule intact.
bool EnableWorkshopEarlyWaitSnapshot()
{
	std::string value = GetSettingValue("ImportDiagnostics", "bWorkshopEarlyWaitSnapshot", "0");
	trim(value);
	return iequals(value, "true") || value == "1";
}

// Opt-in contention experiment, snapshotted when a workshop job is constructed.
// Only changes admission with Use Throttling On; missing/malformed means Off.
bool UseSingleWorkshopCallback()
{
	std::string value = GetSettingValue("ImportDiagnostics", "bSingleWorkshopCallback", "0");
	trim(value);
	return iequals(value, "true") || value == "1";
}

// An explicit valid limit takes precedence over the older single-call switch.
// Missing/malformed preserves the previous policy; 0 means all ready rows.
std::int32_t WorkshopCallbackLimit()
{
	std::string value = GetSettingValue("ImportDiagnostics", "iWorkshopCallbackLimit", "");
	trim(value);
	return Clipboard::WorkshopCallbacks::ParseCallbackLimit(value);
}

// Missing/invalid means rolling admission. With a finite explicit limit and
// Throttling On, zero means fully drain; positive values are a low threshold.
std::int32_t WorkshopCallbackResumeAt()
{
	std::string value = GetSettingValue("ImportDiagnostics", "iWorkshopCallbackResumeAt", "");
	trim(value);
	return Clipboard::WorkshopCallbacks::ParseCallbackLimit(value);
}

// Bounded diagnostic: only the literal 100 selects spacing; absent, zero or
// malformed values retain baseline admission. The job also requires On/single.
std::uint32_t WorkshopCallbackSpacingMs()
{
	std::string value = GetSettingValue("ImportDiagnostics", "iWorkshopCallbackSpacingMs", "0");
	trim(value);
	return Clipboard::WorkshopCallbacks::ParseCallbackSpacing(value);
}

// Retreieve a vector of strings setting.
std::vector<std::string> GetSettingValueStringArray(StaticFunctionTag* base, const BSFixedString section, const BSFixedString key, const std::vector<std::string> defaultValue) {
	std::string resultString = GetSettingValue(section.c_str(), key.c_str(), Join(defaultValue, ',').c_str());
	return split(resultString, ',');
}

// Get the first keyword form with the provided name.
BGSKeyword* GetKeywordByName(const BSFixedString editorID) {
	auto* keyword = TESForm::GetFormByEditorID<BGSKeyword>(editorID);
	if (!keyword) {
		if (auto* dataHandler = TESDataHandler::GetSingleton()) {
			const std::string target{ editorID.c_str() };
			for (auto* candidate : dataHandler->GetFormArray<BGSKeyword>()) {
				if (candidate && iequals(candidate->GetFormEditorID(), target)) {
					keyword = candidate;
					break;
				}
			}
		}
	}
	return keyword;
}

// Load setting values and setup all persistent variables.
void LoadSettings() {
	RefreshLoggingSetting();
	enableVerboseImportLogging = GetSettingValueBool(nullptr, "Debug", "bEnableVerboseImportLogging", false);
	enableAnimalSelection = GetSettingValueBool(nullptr, "Selection", "bEnableAnimalSelection", false);
	// Retired cell-expansion settings are deliberately ignored. Discovery is
	// now automatic; the independent transfer options only change eligibility.
	allowNormallyFilteredObjectsToBeSelectedAndExported = GetSettingValueBool(
		nullptr, "Selection", "bAllowNormallyFilteredObjectsToBeSelectedAndExported", false);
	allowAllObjectsToBeImported = GetSettingValueBool(
		nullptr, "Selection", "bAllowAllObjectsToBeImported", false);

	actorTypeTurretKeyword = GetKeywordByName("ActorTypeTurret");
	actorTypeCreatureKeyword = enableAnimalSelection ? GetKeywordByName("ActorTypeCreature") : nullptr;

	clipboardSelectedKeyword = GetKeywordByName("ClipboardSelected");
	workshopItemKeyword = GetKeywordByName("workshopItemKeyword");
	workshopKeyword = GetKeywordByName("WorkshopKeyword");

	blackListedForms.clear();
	std::vector<std::string> defaultBlackList;
	defaultBlackList.push_back("SimSettlements.esm#103182");

	std::vector<std::string> blackListEntries = GetSettingValueStringArray(nullptr, "Selection", "aFormBlackList", defaultBlackList);
	for (const std::string& blackListEntry : blackListEntries) {
		CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("BlackList: " + std::string(blackListEntry.c_str())));
		std::vector<std::string> blackListEntryParts = split(blackListEntry, '#');
		if (blackListEntryParts.size() == 2) {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("BlackList Item: " + blackListEntryParts[0] + " # " + std::to_string(ToInt(blackListEntryParts[1].c_str()))));
			TemplateItem tItem = TemplateItem();
			tItem.plugin = blackListEntryParts[0];
			tItem.formId = ToInt(blackListEntryParts[1].c_str());
			blackListedForms.push_back(tItem);
		}
	}

	blackListedPlugins.clear();
	const std::vector<std::string> defaultPluginBlackList{
		"SimSettlements.esm",
		"Seasons.esm"
	};
	for (std::string plugin : GetSettingValueStringArray(
			 nullptr, "Selection", "aPluginBlackList", defaultPluginBlackList)) {
		trim(plugin);
		if (!plugin.empty()) {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Plugin BlackList: " + plugin));
			blackListedPlugins.push_back(std::move(plugin));
		}
	}

	std::vector<std::string> blackListedMasters;
	const std::vector<std::string> defaultMasterPluginBlackList{
		"SimSettlements.esm"
	};
	for (std::string master : GetSettingValueStringArray(
			 nullptr, "Selection", "aMasterPluginBlackList", defaultMasterPluginBlackList)) {
		trim(master);
		if (!master.empty()) {
			blackListedMasters.push_back(std::move(master));
		}
	}
	// Rebuild the effective list on every settings reload. The data handler's
	// file list includes full and light plugins; the helper skips inactive files.
	// Expanding once also applies the same policy before pattern FormID lookup.
	if (!blackListedMasters.empty()) {
		if (auto* dataHandler = TESDataHandler::GetSingleton()) {
			for (const auto* file : dataHandler->files) {
				const auto master = Clipboard::Blacklist::FindBlacklistedMaster(file, blackListedMasters);
				if (!master.empty()) {
					const std::string plugin{ file->GetFilename() };
					CLIPBOARD_DEBUG_LOG(logger::info("Master BlackList: {} requires {}", plugin, master));
					if (std::none_of(blackListedPlugins.begin(), blackListedPlugins.end(),
							[&plugin](const std::string& entry) { return iequals(plugin, entry); })) {
						blackListedPlugins.push_back(plugin);
					}
				}
			}
		}
	}
	Clipboard::OptionalFiltering::Configure(
		GetSettingValueStringArray(nullptr, "OptionalFiltering", "aReferenceKeywordBlackList",
			{ "SS2.esm#84480", "Fallout4.esm#2391846" }),
		GetSettingValueStringArray(nullptr, "OptionalFiltering", "aCollectionPluginFamilyBlackList", { "SS2.esm" }),
		GetSettingValueStringArray(nullptr, "OptionalFiltering", "aChildLinkKeywordBlackList",
			split(std::string(Clipboard::OptionalFiltering::kDefaultChildLinkKeywords), ',')),
		GetSettingValueStringArray(nullptr, "OptionalFiltering", "aChildParentScripts",
			{ "ukAutoBeds:ukTablePlaceAtMe", "ukAutoBeds:ukMultiPlaceAtMe" }));
}

// Reload settings call made accessible to papyrus.
void ReloadSettings(StaticFunctionTag* base) {
	LoadSettings();
}

// Get current date/time, format is YYYY-MM-DD.HH:mm:ss
const std::string GetCurrentDateTime() {
	time_t     now = time(0);
	struct tm  tstruct;
	char       buf[80];
	localtime_s(&tstruct,&now);
	strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);
	return buf;
}

//Get the base form in a way that supports both NPCs and objects.
UInt32 GetLowerFormId(const TESForm* baseObj);
BSFixedString GetPluginName(const TESForm* baseObj);

const TESForm* GetBaseForm(const TESObjectREFR* obj) {
	if (!obj) {
		return nullptr;
	}

	TESForm* pBaseForm = obj->data.objectReference;

	if (obj->extraList) {
		const auto* leveledCreature = obj->extraList->GetByType<ExtraLeveledCreature>();
		if (leveledCreature && leveledCreature->baseForm) {
			pBaseForm = leveledCreature->baseForm;
		}
	}
	return pBaseForm;
}

static bool IsPluginBlackListed(const std::string& pluginName)
{
	return std::any_of(
		blackListedPlugins.begin(),
		blackListedPlugins.end(),
		[&pluginName](const std::string& entry) { return iequals(pluginName, entry); });
}

static bool IsFormIdBlackListed(const std::string& pluginName, UInt32 lowerFormId)
{
	return std::any_of(
		blackListedForms.begin(),
		blackListedForms.end(),
		[&pluginName, lowerFormId](const TemplateItem& entry) {
			return iequals(pluginName, entry.plugin) && lowerFormId == entry.formId;
		});
}

static bool IsFormBlackListed(const TESForm* baseForm)
{
	if (!baseForm) {
		return true;
	}

	const std::string pluginName = GetPluginName(baseForm).c_str();
	const UInt32 lowerFormId = GetLowerFormId(baseForm);
	return IsFormIdBlackListed(pluginName, lowerFormId);
}

static bool IsMarkerBaseForm(const TESForm* baseForm)
{
	if (!baseForm) {
		return false;
	}

	auto* baseObject = const_cast<TESForm*>(baseForm)->As<TESObject>();
	return baseObject && baseObject->IsMarker();
}

static bool HasWorkshopPlacementEvidence(const TESObjectREFR* reference)
{
	if (!reference || !reference->IsCreated() || !workshopItemKeyword) {
		return false;
	}

	return GetLinkedRef_Native(
		const_cast<TESObjectREFR*>(reference), workshopItemKeyword) != nullptr;
}

static bool ConstructibleCreatesForm(
	const BGSConstructibleObject* constructible,
	UInt32 formId)
{
	if (!constructible || !formId) {
		return false;
	}

	TESForm* createdItem = constructible->GetCreatedItem();
	if (!createdItem) {
		return false;
	}
	if (createdItem->Is(ENUM_FORM_ID::kFLST)) {
		const auto* createdFormList = createdItem->As<BGSListForm>();
		return createdFormList && std::any_of(
			createdFormList->arrayOfForms.begin(),
			createdFormList->arrayOfForms.end(),
			[formId](const TESForm* createdForm) {
				return createdForm && createdForm->formID == formId;
			});
	}

	return createdItem->formID == formId;
}

static bool IsWorkshopConstructible(const BGSConstructibleObject* constructible)
{
	if (!constructible || !constructible->benchKeyword) {
		return false;
	}

	const char* benchEditorId = constructible->benchKeyword->GetFormEditorID();
	return benchEditorId && istarts_with(benchEditorId, "WorkshopWorkbenchType");
}

static BGSConstructibleObject* FindConstructibleObjectByCreatedObject(
	const TESForm* baseObject,
	bool requireWorkshopBench)
{
	if (!baseObject || !baseObject->formID) {
		return nullptr;
	}

	auto* dataHandler = TESDataHandler::GetSingleton();
	if (!dataHandler) {
		return nullptr;
	}

	for (auto* constructible : dataHandler->GetFormArray<BGSConstructibleObject>()) {
		if (!constructible || constructible->IsDeleted()) {
			continue;
		}
		if (ConstructibleCreatesForm(constructible, baseObject->formID) &&
			(!requireWorkshopBench || IsWorkshopConstructible(constructible))) {
			return constructible;
		}
	}

	return nullptr;
}

static bool HasWorkshopConstructibleRecipe(const TESForm* baseObject)
{
	return FindConstructibleObjectByCreatedObject(baseObject, true) != nullptr;
}

static bool HasNonZeroObjectBounds(const TESForm* baseForm)
{
	if (!baseForm) {
		return false;
	}

	const auto* boundObject = const_cast<TESForm*>(baseForm)->As<TESBoundObject>();
	if (!boundObject) {
		return false;
	}

	const auto& bounds = boundObject->boundData;
	return bounds.boundMin.x != 0 || bounds.boundMin.y != 0 || bounds.boundMin.z != 0 ||
		bounds.boundMax.x != 0 || bounds.boundMax.y != 0 || bounds.boundMax.z != 0;
}

static bool IsBaseFormBlockedForTransfer(const TESForm* baseForm, bool importing, bool selectedForExport = false)
{
	if (!baseForm) {
		return true;
	}

	const auto formType = baseForm->GetFormType();
	if (formType == ENUM_FORM_ID::kBNDS ||
		formType == ENUM_FORM_ID::kTXST) {
		const char* signature = formType == ENUM_FORM_ID::kBNDS ? "BNDS" : "TXST";
		CLIPBOARD_DEBUG_LOG(logger::info(
			"Transfer policy permanently rejected {} base {:08X} from {}",
			signature,
			baseForm->formID,
			GetPluginName(baseForm).c_str()));
		return true;
	}
	if (IsMarkerBaseForm(baseForm)) {
		CLIPBOARD_DEBUG_LOG(logger::info(
			"Transfer policy permanently rejected IsMarker base {:08X} from {}",
			baseForm->formID,
			GetPluginName(baseForm).c_str()));
		return true;
	}
	const bool permissive = selectedForExport || (importing ?
		allowAllObjectsToBeImported :
		allowNormallyFilteredObjectsToBeSelectedAndExported);
	if ((formType == ENUM_FORM_ID::kACTI || formType == ENUM_FORM_ID::kMSTT) && !HasNonZeroObjectBounds(baseForm)) {
		CLIPBOARD_DEBUG_LOG(logger::info("Transfer policy permanently rejected zero-bounds {} base {:08X} from {}",
			formType == ENUM_FORM_ID::kACTI ? "ACTI" : "MSTT", baseForm->formID, GetPluginName(baseForm).c_str()));
		return true;
	}
	if (!permissive && formType == ENUM_FORM_ID::kLIGH && !HasNonZeroObjectBounds(baseForm)) {
		CLIPBOARD_DEBUG_LOG(logger::info(
			"Transfer policy rejected zero-bounds LIGH base {:08X} from {}",
			baseForm->formID,
			GetPluginName(baseForm).c_str()));
		return true;
	}

	const std::string pluginName = GetPluginName(baseForm).c_str();
	if (IsPluginBlackListed(pluginName) || IsFormBlackListed(baseForm)) {
		CLIPBOARD_DEBUG_LOG(logger::info(
			"Transfer policy permanently rejected blacklisted base {:08X} from {}",
			baseForm->formID,
			pluginName));
		return true;
	}

	// Pattern rows do not carry source-reference provenance. A workshop COBJ is
	// durable base-form evidence that a LIGH row represents a buildable light;
	// ambient lights remain unsafe to instantiate from old or external patterns.
	const bool rejectedLight = !permissive && formType == ENUM_FORM_ID::kLIGH &&
		!HasWorkshopConstructibleRecipe(baseForm);
	if (rejectedLight) {
		CLIPBOARD_DEBUG_LOG(logger::info(
			"Transfer policy rejected non-workshop-recipe LIGH base {:08X} from {}",
			baseForm->formID,
			pluginName));
	}
	return rejectedLight;
}

static bool IsReferenceBlockedForAutomaticTransfer(const TESObjectREFR* reference)
{
	if (Clipboard::OptionalFiltering::IsBlocked(reference)) {
		return true;
	}
	const TESForm* baseForm = GetBaseForm(reference);
	if (IsBaseFormBlockedForTransfer(baseForm, false)) {
		return true;
	}

	// Strict automatic selection/export requires both runtime creation and the
	// WorkshopItem link.  An FF FormID alone is not player-authorship evidence,
	// and plugin-placed references must require the explicit permissive switch.
	if (!allowNormallyFilteredObjectsToBeSelectedAndExported) {
		const bool rejectedReference = !HasWorkshopPlacementEvidence(reference);
		if (rejectedReference) {
			CLIPBOARD_DEBUG_LOG(logger::info(
				"Transfer policy rejected reference {:08X}: strict mode requires IsCreated and a live WorkshopItem link",
				reference ? reference->formID : 0));
		}
		return rejectedReference;
	}

	// The explicit permissive switch intentionally admits otherwise-valid
	// authored and runtime-unlinked references.  Loaded 3D is not authorship or
	// per-reference precombine evidence: an authored static can be represented by
	// combined cell geometry and still be a useful base/transform to export.
	// Permanent form exclusions, Clipboard/workbench protection, disabled/deleted
	// reference checks, and the configured blacklists remain authoritative.
	return false;
}

//Filter the given list of objects by those that are in the given box area.
VMArray<TESObjectREFR*> GetObjectsInBox(StaticFunctionTag* base, VMArray<TESObjectREFR*> objs, TESObjectREFR* toolObj, UInt32 xLength, UInt32 yLength, UInt32 zLength) {
	VMArray<TESObjectREFR*> foundObjs;
	if (!toolObj) {
		return foundObjs;
	}

	int halfHeight = zLength * 250;
	int halfWidth = xLength * 250;
	int halfLength = yLength * 250;

	long double cos1 = cos(-toolObj->data.angle.z);
	long double sin1 = sin(-toolObj->data.angle.z);

	float centerX = -(long double)(halfLength + TOOL_OFFSET) * sin1 + toolObj->data.location.x;
	float centerY = (halfLength + TOOL_OFFSET) * cos1 + toolObj->data.location.y;

	long double cos2 = cos(toolObj->data.angle.z);
	long double sin2 = sin(toolObj->data.angle.z);
	long double rotatedX;
	long double rotatedY;

	float relativeX;
	float relativeY;
	float relativeZ;

	TESObjectREFR* obj = nullptr;
	for (UInt32 i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		if (!obj) {
			continue;
		}
		relativeX = centerX - obj->data.location.x;
		relativeY = centerY - obj->data.location.y;
		relativeZ = toolObj->data.location.z - obj->data.location.z;
		rotatedX = relativeX * cos2 - relativeY * sin2;
		rotatedY = relativeY * cos2 + relativeX * sin2;

		if (relativeZ < -halfHeight || relativeZ > halfHeight) {
			continue;
		}

		rotatedX = relativeX * cos2 - relativeY * sin2;
		if (rotatedX < -halfWidth || rotatedX > halfWidth) {
			continue;
		}

		rotatedY = relativeY * cos2 + relativeX * sin2;
		if (rotatedY < -halfLength || rotatedY > halfLength) {
			continue;
		}

		foundObjs.Push(&obj);
	}
	return foundObjs;
}

//Filter the given list of objects by those that are in the given cylinder area.
VMArray<TESObjectREFR*> GetObjectsInCylinder(StaticFunctionTag* base, VMArray<TESObjectREFR*> objs, TESObjectREFR* toolObj, UInt32 radius) {

	VMArray<TESObjectREFR*> foundObjs;
	if (!toolObj) {
		return foundObjs;
	}

	long double cos1 = cos(-toolObj->data.angle.z);
	long double sin1 = sin(-toolObj->data.angle.z);

	float centerX = -(long double)(radius + TOOL_OFFSET) * sin1 + toolObj->data.location.x;
	float centerY = (radius + TOOL_OFFSET) * cos1 + toolObj->data.location.y;

	TESObjectREFR* obj = nullptr;
	for (UInt32 i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		if (!obj) {
			continue;
		}

		if (sqrt(pow(obj->data.location.x - centerX, 2) + pow(obj->data.location.y - centerY, 2)) <= radius) {
			foundObjs.Push(&obj);
		}
	}
	return foundObjs;
}

//Filter the given list of objects by those that are in the given sphere area.
VMArray<TESObjectREFR*> GetObjectsInSphere(StaticFunctionTag* base, VMArray<TESObjectREFR*> objs, TESObjectREFR* toolObj, UInt32 radius) {

	VMArray<TESObjectREFR*> foundObjs;
	if (!toolObj) {
		return foundObjs;
	}

	long double cos1 = cos(-toolObj->data.angle.z);
	long double sin1 = sin(-toolObj->data.angle.z);

	float centerX = -(long double)(radius + TOOL_OFFSET) * sin1 + toolObj->data.location.x;
	float centerY = (radius + TOOL_OFFSET) * cos1 + toolObj->data.location.y;

	TESObjectREFR* obj = nullptr;
	double d[] = { 0,0,0 };
	for (UInt32 i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		if (!obj) {
			continue;
		}

		d[0] = abs(centerX - obj->data.location.x);
		d[1] = abs(centerY - obj->data.location.y);
		d[2] = abs(toolObj->data.location.z - obj->data.location.z);

		if (d[0] < d[1])
			std::swap(d[0], d[1]);
		if (d[0] < d[2])
			std::swap(d[0], d[2]);

		if (d[0] * sqrt(1.0 + d[1] / d[0] + d[2] / d[0]) <= radius) {
			foundObjs.Push(&obj);
		}
	}
	return foundObjs;
}

/*
* Copied from PapyrusObjectReference.cpp to avoid build complications.
*/
template <class T>
T* GetDefaultForm(std::string_view a_editorID)
{
	auto* defaultObject = TESForm::GetFormByEditorID<BGSDefaultObject>(BSFixedString{ a_editorID });
	return defaultObject ? defaultObject->GetForm<T>() : nullptr;
}

VirtualMachine* GetVirtualMachine()
{
	const auto* gameVM = GameVM::GetSingleton();
	return gameVM ? gameVM->GetVM().get() : nullptr;
}

class ScopedCurrentWorkshop
{
public:
	explicit ScopedCurrentWorkshop(TESObjectREFR* a_workshop) :
		_previous(Clipboard::EngineAPI::CurrentWorkshop())
	{
		Clipboard::EngineAPI::SetCurrentWorkshop(a_workshop ? a_workshop->GetHandle() : ObjectRefHandle{});
	}

	~ScopedCurrentWorkshop()
	{
		Clipboard::EngineAPI::SetCurrentWorkshop(_previous);
	}

	ScopedCurrentWorkshop(const ScopedCurrentWorkshop&) = delete;
	ScopedCurrentWorkshop& operator=(const ScopedCurrentWorkshop&) = delete;

private:
	ObjectRefHandle _previous;
};

/*
 * Copied from PapyrusObjectReference.cpp to avoid build complications.
*/

TESObjectREFR* AttachWireLatent(UInt32 stackId, TESObjectREFR* refA, TESObjectREFR* refB, TESForm* splineForm)
{
	if (!Clipboard::EngineTaskDispatch::InTask()) {
		logger::error("Clipboard wire attachment rejected outside the engine task queue");
		return nullptr;
	}
	TESObjectREFR* wireRef = nullptr;
	auto* gameVM = GameVM::GetSingleton();
	auto vmOwner = gameVM ? gameVM->GetVM() : nullptr;
	VirtualMachine* vm = vmOwner.get();

	if (!splineForm) {
		splineForm = GetDefaultForm<BGSBendableSpline>("WorkshopSplineObject");
	}

	// No specified spline, no refs, refs are same item, or no 3D loaded
	if (!vm || !splineForm || !refA || !refB || refA == refB || !refA->Get3D() || !refB->Get3D()) {
		logger::warn(
			"AttachWire stack {} rejected prerequisites: vm={}, spline={}, refA={}, refB={}, "
			"sameRef={}, refA3D={}, refB3D={}",
			stackId,
			vm != nullptr,
			splineForm != nullptr,
			refA != nullptr,
			refB != nullptr,
			refA && refB && refA == refB,
			refA && refA->Get3D(),
			refB && refB->Get3D());
		return nullptr;
	}

	// See if the two references are already linked by the same wire i.e. they have the same entry in their PowerLinks listing
	std::set<UInt32> linkedWires;
	ExtraDataList* extraDataRefA = refA->extraList.get();
	ExtraDataList* extraDataRefB = refB->extraList.get();
	if (extraDataRefA && extraDataRefB)
	{
		ExtraPowerLinks* powerLinksA = extraDataRefA->GetByType<ExtraPowerLinks>();
		ExtraPowerLinks* powerLinksB = extraDataRefB->GetByType<ExtraPowerLinks>();
		if (powerLinksA && powerLinksB) // Both items must have power links to check
		{
			BSTArray<ExtraPowerLinks::Element>* connectionSearch;
			BSTArray<ExtraPowerLinks::Element>* connectionPopulate;
			if (powerLinksA->powerLinks.size() < powerLinksB->powerLinks.size()) // Pick the smaller list to be the set
			{
				connectionPopulate = &powerLinksA->powerLinks;
				connectionSearch = &powerLinksB->powerLinks;
			}
			else
			{
				connectionPopulate = &powerLinksB->powerLinks;
				connectionSearch = &powerLinksA->powerLinks;
			}

			// Add the items from the smaller list to the set
			for (const auto& connection : *connectionPopulate)
			{
				linkedWires.insert(connection.formID);
			}

			// Search the other listing for items that exist in the set
			for (const auto& connection : *connectionSearch)
			{
				// This wire exists in the other list, it is invalid to wire the same objects twice
				if (linkedWires.contains(connection.formID)) {
					logger::warn(
						"AttachWire stack {} skipped duplicate endpoints {:08X} and {:08X} "
						"sharing wire {:08X}",
						stackId,
						refA->formID,
						refB->formID,
						connection.formID);
					return nullptr;
				}
			}
		}
	}

	BGSBendableSpline* spline = DYNAMIC_CAST(splineForm, TESForm, BGSBendableSpline);
	BGSBendableSpline* splineA = DYNAMIC_CAST(refA->data.objectReference, TESForm, BGSBendableSpline);
	BGSBendableSpline* splineB = DYNAMIC_CAST(refB->data.objectReference, TESForm, BGSBendableSpline);

	BGSKeyword* keyword = GetDefaultForm<BGSKeyword>("WorkshopItem");

	// No workshop keyword is bad
	// Connecting a wire to another wire or connecting a non-wire is invalid
	if (!keyword || !spline || splineA || splineB) {
		logger::warn(
			"AttachWire stack {} rejected form types: WorkshopItem={}, spline={}, "
			"endpointAIsSpline={}, endpointBIsSpline={}",
			stackId,
			keyword != nullptr,
			spline != nullptr,
			splineA != nullptr,
			splineB != nullptr);
		return nullptr;
	}

	// Get the workshop by keyword
	TESObjectREFR* workshopRef = GetLinkedRef_Native(refA, keyword);
	if (!workshopRef) {
		logger::warn(
			"AttachWire stack {} endpoint {:08X} has no WorkshopItem owner",
			stackId,
			refA->formID);
		return nullptr;
	}

	// Workshop ref isn't a workshop!
	Workshop::ExtraData* extraDataWorkshop = workshopRef->extraList ?
		workshopRef->extraList->GetByType<Workshop::ExtraData>() : nullptr;
	if (!extraDataWorkshop) {
		logger::warn(
			"AttachWire stack {} workshop {:08X} has no workshop extra data",
			stackId,
			workshopRef->formID);
		return nullptr;
	}

	auto* player = PlayerCharacter::GetSingleton();
	if (!player || !player->parentCell) {
		logger::warn(
			"AttachWire stack {} has no player or loaded player cell",
			stackId);
		return nullptr;
	}

	// Create our wire instance
	wireRef = PlaceAtMe_Native(vm, stackId, &refA, spline, 1, true, true, false);
	if (!wireRef) {
		logger::warn(
			"AttachWire stack {} failed to place the spline for endpoints {:08X} and {:08X}",
			stackId,
			refA->formID,
			refB->formID);
		return nullptr;
	}

	UInt32 nullHandle = Clipboard::EngineAPI::InvalidRefHandle();
	TESObjectCELL* parentCell = wireRef->parentCell;
	TESWorldSpace* worldspace = wireRef->GetWorldSpace();

	NiPoint3 rot{};
	MoveRefrToPosition(wireRef, &nullHandle, parentCell, worldspace, &refA->data.location, &rot);

	// Set the wire's linked ref to the workshop
	SetLinkedRef_Native(wireRef, workshopRef, keyword);

	ScopedCurrentWorkshop currentWorkshop{ workshopRef };

	Workshop::ContextData contextData(player);
	Clipboard::EngineAPI::UpdateSpline(&contextData, wireRef, refB, 0, refA, 0);
	SplineUtils::ConnectSpline(refA, 0, refB, 0, wireRef);

	ExtraBendableSplineParams* splineParams = wireRef->extraList ?
		wireRef->extraList->GetByType<ExtraBendableSplineParams>() : nullptr;
	if (splineParams) {
		splineParams->thickness = 1.5f;
	}

	Clipboard::EngineAPI::WorkshopExtraAddItem(extraDataWorkshop, wireRef);
	Clipboard::EngineAPI::WorkshopExtraAddConnection(extraDataWorkshop, refA, refB, wireRef);
	Clipboard::EngineAPI::UpdateMovingWirelessItem(refA, extraDataWorkshop);
	Clipboard::EngineAPI::UpdateMovingWirelessItem(refB, extraDataWorkshop);
	Clipboard::EngineAPI::EstablishTerminalLinks(wireRef);
	return wireRef;
}

// Get the scale value of the given object, converted to a float.
float GetScale(const TESObjectREFR* obj) {
	return obj ? static_cast<float>(obj->refScale) * 0.01F : 1.0F;
}

//Get the index of the cell in the given list with the given id.
//TODO generalize to work with all TESForms
int GetCellIndexById(VMArray<TESObjectCELL*> objs, const UInt64 id) {

	TESObjectCELL* obj = nullptr;
	for (UInt32 i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		if (obj && obj->formID == id) {
			return static_cast<int>(i);
		}
	}
	return -1;
}


//Get the index of the obj in the given list with the given id.
//TODO generalize to work with all TESForms
int GetObjectReferenceIndexById(const VMArray<TESObjectREFR*>& objs, const UInt64 id) {

	TESObjectREFR* obj = nullptr;
	for (UInt32 i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		if (obj) {
			if (obj->formID == id) {
				return static_cast<int>(i);
			}
		}
	}
	return -1;
}

static Clipboard::Selection::FirstReferenceRows GetReferenceRows(const VMArray<TESObjectREFR*>& objects)
{
	return Clipboard::Selection::BuildFirstReferenceRows(objects.Length(), [&objects](UInt32 row) -> std::optional<UInt32> {
		TESObjectREFR* object = nullptr;
		objects.Get(&object, row);
		return object ? std::optional<UInt32>{ object->formID } : std::nullopt;
	});
}

//Get the path of the clipboard pattern in the given slot.
std::string GetPatternFilePath(const UInt32 slot) {
	std::string s_configPath;
	const std::string runtimePath = GetRuntimeDirectory();
	if (!runtimePath.empty()) {
		s_configPath = runtimePath;
		s_configPath += "Data\\F4SE\\plugins\\clipboard\\";
		s_configPath += std::to_string(slot);
		s_configPath += "\\pattern.ini";
	}
	return s_configPath;
}

//Get the path of the blueprint in the given slot.
// INCOMPLETE, WORK IN PROGRESS
std::string GetBlueprintFilePath(const UInt32 slot) {
	std::string s_configPath;
	const std::string runtimePath = GetRuntimeDirectory();
	if (!runtimePath.empty()) {
		s_configPath = runtimePath;
		s_configPath += "Data\\F4SE\\plugins\\TransferSettlements\\blueprints\\";
		s_configPath += std::to_string(slot);
		WIN32_FIND_DATAA FindFileData;
		HANDLE h = FindFirstFileA((s_configPath + "\\*.json").c_str(), &FindFileData);
		if (h == INVALID_HANDLE_VALUE) {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("FindFirstFile.  Err=" + std::to_string(GetLastError())));
			return {};
		}
		CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found Blueprint: " + std::string(FindFileData.cFileName)));
		FindClose(h);
		return s_configPath + "\\" + FindFileData.cFileName;
	}
	return s_configPath;
}

// Reads a single value from the given clipboard pattern.
BSFixedString ReadPatternSectionValue(const UInt32 slot, const BSFixedString section, const BSFixedString key) {
	std::string configPath = GetPatternFilePath(slot);

	std::string sectionLine(section);
	sectionLine = "[" + sectionLine + "]";

	std::ifstream inFile;
	std::string targetKey(key);
	trim(targetKey);
	std::string currentKey;
	std::string line;
	BSFixedString lineFixed;
	std::string::size_type valueIndex;
	bool inSection = false;
	inFile.open(configPath.c_str());
	if (inFile) {
		while (std::getline(inFile, line))
		{
			if (line.empty()) {
				continue;
			}
			if (line.at(0) == '[' && line.back() == ']')
			{
				if (inSection)
					break;

				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found Section: " + line));
				inSection = iequals(sectionLine, line);
			}
			else if (inSection)
			{
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found Line: " + line));
				valueIndex = line.find('=');
				if (valueIndex == std::string::npos) {
					continue;
				}
				currentKey = line.substr(0, valueIndex);
				trim(currentKey);
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found Key: " + currentKey));
				if (currentKey == targetKey)
				{
					line = line.substr(valueIndex + 1);
					trim(line);
					CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found Value: " + line));
					return line.c_str();
				}
			}
		}
		inFile.close();
	}

	return BSFixedString("");
}

// Get a list of all objects currently selected.
VMArray<TESObjectREFR*> GetSelectedObjectReferences(StaticFunctionTag* base, TESObjectREFR* refObj) {
	std::set<TESObjectREFR*> linkedObjs;
	VMArray<TESObjectREFR*> objects;
	TESObjectREFR* currentObj = refObj;
	do {
		currentObj = (TESObjectREFR*)GetLinkedRef_Native(currentObj, clipboardSelectedKeyword);;

		if (!currentObj) {
			break;
		}
		else if (refObj == currentObj) {
			break;
		}
		else if (!currentObj->formID) {
			break;
		}
		else if (currentObj->formID <= 0) {
			break;
		}
		else if (linkedObjs.contains(currentObj)) {
			break;
		}

		objects.Push(&currentObj);
		linkedObjs.insert(currentObj);
	} while (true);
	CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Selected Objects Found: " + std::to_string(objects.Length())));
	return objects;
}

// Count how many of the given objects are fully loaded.
UInt32 GetFullyLoadedCount(StaticFunctionTag* base, VMArray<TESObjectREFR*> objs) {
	int count = 0;
	TESObjectREFR* obj;
	for (UInt32 i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);

		if (obj && obj->Get3D()) {
			count++;
		}
	}
	return count;
}

// Check if all selected objects have their 3d modals fullt loaded.
bool IsSelectionFullyLoaded(StaticFunctionTag* base, TESObjectREFR* refObj) {
	std::set<TESObjectREFR*> linkedObjs;
	TESObjectREFR* currentObj = refObj;
	do {
		currentObj = (TESObjectREFR*)GetLinkedRef_Native(currentObj, clipboardSelectedKeyword);;

		if (!currentObj) {
			break;
		}
		else if (refObj == currentObj) {
			break;
		}
		else if (!currentObj->formID) {
			break;
		}
		else if (currentObj->formID <= 0) {
			break;
		}
		else if (linkedObjs.contains(currentObj)) {
			break;
		}
		else if (!currentObj->Get3D()) {
			return false;
		}

		linkedObjs.insert(currentObj);
	} while (true);

	return true;
}

// Count how many objects are currently selected.
UInt32 GetSelectionCount(StaticFunctionTag* base, TESObjectREFR* refObj) {
	VMArray<TESObjectREFR*> objs = GetSelectedObjectReferences(base, refObj);
	return objs.Length();
}

// Return one selected reference without making Papyrus rebuild the chain.
// The deployed script declared this native even though the historical DLL did
// not bind it; Phase 3 closes that registration gap.
TESObjectREFR* GetSelectedObjectReference(StaticFunctionTag* base, UInt32 index, TESObjectREFR* refObj) {
	VMArray<TESObjectREFR*> objs = GetSelectedObjectReferences(base, refObj);
	TESObjectREFR* result = nullptr;
	return objs.Get(&result, index) ? result : nullptr;
}

// F4SE input key codes use DirectInput scan codes for 0..255, followed by
// mouse and XInput pseudo-keys. Keep this small helper local so no legacy F4SE
// InputMap implementation is linked into the CommonLib target.
BSFixedString GetKeyName(StaticFunctionTag*, UInt32 keyCode) {
	if (keyCode < 256) {
		std::array<char, 128> name{};
		LONG keyNameParam = static_cast<LONG>(keyCode << 16);
		if (keyCode == 0x9C || keyCode == 0x9D || keyCode == 0xB5 ||
			keyCode == 0xB7 || keyCode == 0xB8 || keyCode == 0xC7 ||
			keyCode == 0xC8 || keyCode == 0xC9 || keyCode == 0xCB ||
			keyCode == 0xCD || keyCode == 0xCF || keyCode == 0xD0 ||
			keyCode == 0xD1 || keyCode == 0xD2 || keyCode == 0xD3) {
			keyNameParam |= 1 << 24;
		}
		if (GetKeyNameTextA(keyNameParam, name.data(), static_cast<int>(name.size())) > 0) {
			return BSFixedString{ name.data() };
		}
	}
	if (keyCode >= 256 && keyCode < 264) {
		const auto number = std::to_string(keyCode - 255);
		return BSFixedString{ ("Mouse " + number).c_str() };
	}
	if (keyCode == 264) {
		return BSFixedString{ "Mouse Wheel Up" };
	}
	if (keyCode == 265) {
		return BSFixedString{ "Mouse Wheel Down" };
	}
	static constexpr std::array<std::string_view, 16> gamepadNames{
		"D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
		"Start", "Back", "Left Stick", "Right Stick",
		"Left Shoulder", "Right Shoulder", "A", "B", "X", "Y",
		"Left Trigger", "Right Trigger"
	};
	if (keyCode >= 266 && keyCode < 282) {
		return BSFixedString{ gamepadNames[keyCode - 266] };
	}
	return BSFixedString{};
}

// Builds the full form id from its base form id and the plugin name.
UInt32 GetFullFormId(const UInt32 lowerFormId, const BSFixedString pluginName) {
	UInt32 formId = 0;

	if (pluginName == "Fallout4.esm") {
		CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Vanilla Form " + std::to_string(lowerFormId)));
		return lowerFormId;
	}

	auto* dataHandler = TESDataHandler::GetSingleton();
	const auto* file = dataHandler ? dataHandler->LookupModByName(pluginName.c_str()) : nullptr;
	if (file && file->IsActive() && file->IsLight()) {
		formId = 0xFE000000 |
			(static_cast<UInt32>(file->GetSmallFileCompileIndex()) << 12) |
			(lowerFormId & 0xFFF);
		CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Light Plugin " + std::string(pluginName.c_str()) + "#" + std::to_string(lowerFormId) + " = " + std::to_string(formId)));
		return formId;
	}
	if (file && file->IsActive()) {
		formId = (static_cast<UInt32>(file->GetCompileIndex()) << 24) | (lowerFormId & 0xFFFFFF);
		CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Normal Plugin " + std::string(pluginName.c_str()) + "#" + std::to_string(lowerFormId) + " = " + std::to_string(formId)));
		return formId;
	}

	AppendToLog("Mod Missing! " + std::string(pluginName.c_str()) + "#" + std::to_string(lowerFormId));
	return BOTTLECAP_FORM_ID;
}

// Get the form of the given object with the plugin index data removed.
UInt32 GetLowerFormId(const TESForm* baseObj) {
	if (!baseObj) {
		return 0;
	}
	if (baseObj->formID >= 0xFE000000 && baseObj->formID <= 0xFFFFFFFF) {
		UInt32 formId = baseObj->formID & 0xFFF;
		CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Light Plugin Form " + std::to_string(baseObj->formID) + " = " + std::to_string(formId)));
		return formId;
	}
	UInt32 formId = baseObj->formID & 0xFFFFFF;
	CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Normal Plugin Form " + std::to_string(baseObj->formID) + " = " + std::to_string(formId)));
	return formId;
}

//Get the name of the plugin that the given objects came from.
BSFixedString GetPluginName(const TESForm* baseObj) {
	if (baseObj && baseObj->formID >= 0x01000000) {
		if (const auto* file = baseObj->GetFile(0)) {
			return BSFixedString{ file->GetFilename() };
		}
	}
	return "Fallout4.esm";
}

// Play the given shader form on the given object.
// TODO: Pass the shader as a TESEffectShader initially.
void ApplyShaderEffect(StaticFunctionTag* base, TESObjectREFR* obj, TESForm* effectShaderForm) {
	TESEffectShader* effectShader = DYNAMIC_CAST(effectShaderForm, TESForm, TESEffectShader);
	if (effectShader)
		Clipboard::EngineAPI::PlayEffectShader(obj, effectShader);
}

// Stop the given shader form on the given object.
// TODO: Pass the shader as a TESEffectShader initially.
void RemoveShaderEffect(StaticFunctionTag* base, TESObjectREFR* obj, TESForm* effectShaderForm) {
	TESEffectShader* effectShader = DYNAMIC_CAST(effectShaderForm, TESForm, TESEffectShader);
	if (effectShader)
		Clipboard::EngineAPI::StopEffectShader(obj, effectShader);
}

// Play the given shader form on all selected objects.
// TODO: Pass the shader as a TESEffectShader initially.
void ApplyShaderEffectToSelection(StaticFunctionTag* base, TESObjectREFR* refObj, TESForm* effectShaderForm) {
	VMArray<TESObjectREFR*> objs = GetSelectedObjectReferences(base, refObj);
	TESObjectREFR* obj;
	for (UInt32 i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		ApplyShaderEffect(base, obj, effectShaderForm);
	}
}

// Stop the given shader form on all selected objects.
// TODO: Pass the shader as a TESEffectShader initially.
void RemoveShaderEffectToSelection(StaticFunctionTag* base, TESObjectREFR* refObj, TESForm* effectShaderForm) {
	VMArray<TESObjectREFR*> objs = GetSelectedObjectReferences(base, refObj);
	TESObjectREFR* obj;
	for (UInt32 i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		RemoveShaderEffect(base, obj, effectShaderForm);
	}
}

// Deselect all currently selected objects.
// TODO: Pass the shader as a TESEffectShader initially.
UInt32 ClearSelection(StaticFunctionTag* base, TESObjectREFR* refObj, TESForm* effectShader) {
	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(base, refObj);
	TESObjectREFR* currentObj;

	for (UInt32 i = 0; i < selectedObjs.Length(); i++) {
		selectedObjs.Get(&currentObj, i);
		SetLinkedRef_Native(currentObj, nullptr, clipboardSelectedKeyword);
		RemoveShaderEffect(base, currentObj, effectShader);
	}
	SetLinkedRef_Native(refObj, nullptr, clipboardSelectedKeyword);
	return selectedObjs.Length();
}

// Deselect the given object.
// TODO: Pass the shader as a TESEffectShader initially.
bool Deselect(StaticFunctionTag* base, TESObjectREFR* refObj, TESObjectREFR* obj, TESForm* effectShader) {
	if (!refObj || !obj) {
		return false;
	}

	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(nullptr, refObj);
	int index = GetObjectReferenceIndexById(selectedObjs, obj->formID);
	if (index < 0) {
		return false;
	}

	TESObjectREFR* prevObj;
	if (index == 0) {
		prevObj = refObj;
	}
	else {
		selectedObjs.Get(&prevObj, index - 1);
	}

	TESObjectREFR* nextObj = GetLinkedRef_Native(obj, clipboardSelectedKeyword);
	SetLinkedRef_Native(prevObj, nextObj, clipboardSelectedKeyword);
	SetLinkedRef_Native(obj, nullptr, clipboardSelectedKeyword);
	RemoveShaderEffect(base, obj, effectShader);

	if (!Clipboard::EngineAPI::CallFunctionNoWait(GetVirtualMachine(), refObj, "RemoveShaderEffect", obj)) {
		logger::error("Failed to call RemoveShaderEffect on selection reference");
	}
	return true;
}

// Deselect all the given objects.
// TODO: Pass the shader as a TESEffectShader initially.
UInt32 DeselectAll(StaticFunctionTag* base, TESObjectREFR* refObj, VMArray<TESObjectREFR*> objs, TESForm* effectShader) {
	TESObjectREFR* currentObj;
	UInt32 count = 0;
	for (UInt32 i = 0; i < objs.Length(); i++) {
		objs.Get(&currentObj, i);
		if (Deselect(base, refObj, currentObj, effectShader)) {
			count++;
		}
	}

	return count;
}

// Get the end of the chain of selected objects
// TODO: Provide a way to directly access the last selected object without having to walk the chain. (A second keyword link?)
TESObjectREFR* GetLastSelectedObject(const VMArray<TESObjectREFR*>& selectedObjs, TESObjectREFR* refObj) {
	TESObjectREFR* lastObj;
	if (selectedObjs.Length() > 0) {
		selectedObjs.Get(&lastObj, selectedObjs.Length() - 1);
	}
	else {
		lastObj = refObj;
	}
	return lastObj;
}

//Test if the given form is a workshop.
bool IsWorkshop(const TESForm* thisForm) {
	if (!thisForm || !workshopKeyword)
		return false;

	UInt32 workbenchKeywordFormId = workshopKeyword->formID;
	const BGSKeywordForm* pKeywords = DYNAMIC_CAST(thisForm, TESForm, BGSKeywordForm);
	if (pKeywords) {
		for (UInt32 i = 0; i < pKeywords->numKeywords; i++)
		{
			if (pKeywords->keywords[i] && pKeywords->keywords[i]->formID == workbenchKeywordFormId)
				return true;
		}
	}
	return false;
}

//Filter Actor's using their race.
//   * Allow Manneqins by race name.
//   * Allow Turrets using the actor type keyword. actorTypeTurretKeyword
//   * Allow Creatures using the actor type keyword. actorTypeCreatureKeyword
bool FilterByRace(const TESRace* race) {
	if (!race) {
		return false;
	}
	if (MANNEQUIN_RACE_EDITOR_ID == race->formEditorID.c_str()) {
		return true;
	}

	const auto* raceKeywords = static_cast<const BGSKeywordForm*>(race);
	for (UInt32 i = 0; i < raceKeywords->numKeywords; i++) {
		BGSKeyword* keyword = raceKeywords->keywords[i];
		if (keyword) {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("\t\t" + std::to_string(keyword->formID) + " " + keyword->GetFormEditorID()));
			if (actorTypeTurretKeyword && keyword->formID == actorTypeTurretKeyword->formID) {
				return true;
			}
			if (actorTypeCreatureKeyword && keyword->formID == actorTypeCreatureKeyword->formID) {
				return true;
			}
		}
	}
	return false;
}

static bool IsSupportedNPCBaseForm(const TESNPC* npc)
{
	if (!npc) {
		return true;
	}
	if (npc->GetFormRace() && FilterByRace(npc->GetFormRace())) {
		return true;
	}
	const auto* templateNPC = npc->baseTemplateForm ? npc->baseTemplateForm->As<TESNPC>() : nullptr;
	return templateNPC && templateNPC->GetFormRace() && FilterByRace(templateNPC->GetFormRace());
}

// These checks describe whether a resolved pattern row can safely be handed to
// PlaceAtMe at all. They are structural guards, not user-overridable content
// preferences such as TXST/SCOL/LIGH policy or explicit blacklists.
static bool IsBaseFormStructurallyBlockedForImport(TESForm* baseForm)
{
	if (!baseForm || baseForm->IsDeleted()) {
		return true;
	}
	if (!baseForm->As<TESBoundObject>()) {
		return true;
	}
	if (baseForm->GetFormType() == ENUM_FORM_ID::kLVLN) {
		return true;
	}
	return !IsSupportedNPCBaseForm(baseForm->As<TESNPC>());
}

//Check if an object can selected or not.
//   * Must not already be selected.
//   * Must not be a wire.
//   * Must not be a workshop.
//   * Must not be disabled or deleted.
//   * Must not inherit from a workshop form.
//   * Must not be introduced by Clipboard.
//   * Must pass the FilterByRace function.
bool FilterSelection(const VMArray<TESObjectREFR*>* selectedObjs, const TESObjectREFR* obj,
	const Clipboard::Selection::FirstReferenceRows* selectedRows = nullptr) {
	const auto* splineDefault = GetDefaultForm<BGSBendableSpline>("WorkshopSplineObject");
	const UInt64 splineFormId = splineDefault ? splineDefault->formID : 0;
	// Don't select if empty or a wire.
	if (!obj || !obj->data.objectReference || obj->formID == 0 || obj->data.objectReference->formID == splineFormId) {
		return false;
	}
	// Don't select settlement workbenches.
	if (IsWorkshop(obj)) {
		return false;
	}
	// Don't select if already selected.
	if (selectedRows ? selectedRows->Find(obj->formID) >= 0 :
		(selectedObjs && GetObjectReferenceIndexById(*selectedObjs, obj->formID) >= 0)) {
		return false;
	}
	//Don't select if disabled or deleted.
	if ((obj->GetFormFlags() & ((1u << 5) | (1u << 11))) != 0) {
		return false;
	}

	const TESForm* baseForm = GetBaseForm(obj);

	// Don't select workbenches.
	if (IsWorkshop(baseForm)) {
		return false;
	}
	//Don't select if from the clipboard mod.
	std::string pluginName = GetPluginName(baseForm).c_str();
	if (istarts_with(pluginName, "Clipboard.")) {
		CLIPBOARD_DEBUG_LOG(logger::info(
			"Selection rejected Clipboard-owned reference {:08X} (base {:08X}, source {})",
			obj->formID,
			baseForm ? baseForm->formID : 0,
			pluginName));
		return false;
	}

	// Optional content exclusions apply to scanning and explicit selection too.
	// Strict placement evidence remains separate in automatic selection/export.
	if (Clipboard::OptionalFiltering::IsBlocked(obj)) {
		return false;
	}

	const Actor* actor = DYNAMIC_CAST(obj, TESForm, Actor);
	if (actor) {
		// Resolve leveled actors through GetBaseForm before applying the same
		// concrete-NPC rule used by import. An unresolved actor base is not a
		// transferable workshop object.
		const TESNPC* npc = baseForm ? const_cast<TESForm*>(baseForm)->As<TESNPC>() : nullptr;
		if (npc && IsSupportedNPCBaseForm(npc)) {
			return true;
		}
		return false;
	}
	return true;
}

// Apply transfer-safety filtering to broad/automatic selection modes. The
// single-reference Select function retains baseline and optional content
// checks above, while console/gun selection can bypass strict placement checks.
bool FilterAutomaticSelection(const VMArray<TESObjectREFR*>* selectedObjs, const TESObjectREFR* obj,
	const Clipboard::Selection::FirstReferenceRows* selectedRows = nullptr) {
	if (!FilterSelection(selectedObjs, obj, selectedRows)) {
		return false;
	}
	if (IsReferenceBlockedForAutomaticTransfer(obj)) {
		CLIPBOARD_DEBUG_LOG(
			const TESForm* baseForm = GetBaseForm(obj);
			logger::info(
			"Automatic selection rejected reference {:08X} (base {:08X}, source {})",
			obj ? obj->formID : 0,
			baseForm ? baseForm->formID : 0,
			baseForm ? GetPluginName(baseForm).c_str() : ""));
		return false;
	}
	return true;
}

// Select the given object.
// TODO: Pass the shader as a TESEffectShader initially.
bool Select(StaticFunctionTag* base, TESObjectREFR* refObj, TESObjectREFR* obj, TESForm* effectShader) {
	Clipboard::OptionalFiltering::ScopedScan optionalScan;
	if (!refObj || !clipboardSelectedKeyword) {
		return false;
	}
	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(base, refObj);
	TESObjectREFR* lastSelectedObj = GetLastSelectedObject(selectedObjs, refObj);

	const bool eligible = refObj != obj && FilterSelection(&selectedObjs, obj);
	if (eligible) {
		SetLinkedRef_Native(lastSelectedObj, obj, clipboardSelectedKeyword);
		SetLinkedRef_Native(obj, nullptr, clipboardSelectedKeyword);
		ApplyShaderEffect(base, obj, effectShader);
		return true;
	}
	return false;
}

// Select all of the given objects.
// TODO: Pass the shader as a TESEffectShader initially.
UInt32 SelectAll(StaticFunctionTag* base, TESObjectREFR* refObj, VMArray<TESObjectREFR*> objs, TESForm* effectShader) {
	Clipboard::OptionalFiltering::ScopedScan optionalScan;
	if (!refObj || !clipboardSelectedKeyword) {
		return 0;
	}

	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(base, refObj);
	TESObjectREFR* lastSelectedObj = GetLastSelectedObject(selectedObjs, refObj);
	// Match the existing selection snapshot. Do not change how repeated rows
	// within the incoming array are handled while optimizing membership reads.
	const auto selectedRows = GetReferenceRows(selectedObjs);

	TESObjectREFR* currentObj;
	UInt32 count = 0;
	for (UInt32 i = 0; i < objs.Length(); i++) {
		objs.Get(&currentObj, i);
		if (currentObj && refObj != currentObj && FilterAutomaticSelection(nullptr, currentObj, &selectedRows)) {
			SetLinkedRef_Native(lastSelectedObj, currentObj, clipboardSelectedKeyword);
			ApplyShaderEffect(base, currentObj, effectShader);
			lastSelectedObj = currentObj;
			count++;
		}
	}

	SetLinkedRef_Native(lastSelectedObj, nullptr, clipboardSelectedKeyword);

	return count;
}

// Fix wire positions after programmatically moving/scaling the objects they are attached to.
VMArray<TESObjectREFR*> UpdateSelectedWires(const VMArray<TESObjectREFR*>& objs) {
	const auto* splineDefault = GetDefaultForm<BGSBendableSpline>("WorkshopSplineObject");
	const UInt32 splineFormId = splineDefault ? splineDefault->formID : 0;
	VMArray<TESObjectREFR*> wires;
	TESObjectREFR* obj;
	std::set<UInt32> processedWires;
	for (UInt32 i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		if (obj) {
			ExtraDataList* extraDataList = obj->extraList.get();
			if (extraDataList) {
				ExtraPowerLinks* powerLinks = extraDataList->GetByType<ExtraPowerLinks>();
				if (powerLinks) {
					for (const auto& connection : powerLinks->powerLinks) {
						TESForm* form = LookupFormByID(connection.formID);
						if (form) {
							TESObjectREFR* wireRef = DYNAMIC_CAST(form, TESForm, TESObjectREFR);
							if (wireRef && wireRef->data.objectReference &&
								wireRef->data.objectReference->formID == splineFormId &&
								processedWires.insert(wireRef->formID).second) {
								//We Found a Wire!
								ExtraPowerLinks* wirePowerLinks = wireRef->extraList ?
									wireRef->extraList->GetByType<ExtraPowerLinks>() : nullptr;
								if (wirePowerLinks && wirePowerLinks->powerLinks.size() == 2) {
									TESObjectREFR* obj1Ref = DYNAMIC_CAST(
										LookupFormByID(wirePowerLinks->powerLinks[0].formID), TESForm, TESObjectREFR);
									TESObjectREFR* obj2Ref = DYNAMIC_CAST(
										LookupFormByID(wirePowerLinks->powerLinks[1].formID), TESForm, TESObjectREFR);
									if (obj1Ref && obj2Ref) {
										auto* workshopRef = GetLinkedRef_Native(wireRef, workshopItemKeyword);
										if (!workshopRef) {
											workshopRef = Workshop::FindNearestValidWorkshop(*obj1Ref);
										}
									if (workshopRef) {
										auto* player = PlayerCharacter::GetSingleton();
										if (player && player->parentCell) {
											ScopedCurrentWorkshop currentWorkshop{ workshopRef };
											Workshop::ContextData contextData(player);
											Clipboard::EngineAPI::UpdateSpline(&contextData, wireRef, obj2Ref, 0, obj1Ref, 0);
										}
									}
									}
									wires.Push(&wireRef);
								}
							}
						}
					}
				}
			}
		}
	}
	return wires;
}

VMArray<TESObjectREFR*> UpdateSelectedWires_(StaticFunctionTag* base, TESObjectREFR* refObj) {
	return UpdateSelectedWires(GetSelectedObjectReferences(base, refObj));
}

// Rotate all selected objects around the z-axis of the given origin by the given number of degrees.
void RotateSelectionZ(StaticFunctionTag* base, TESObjectREFR* refObj, const float zRotation, const float originX, const float originY) {
	if (!refObj) {
		return;
	}
	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(base, refObj);

	UInt32 nullHandle = Clipboard::EngineAPI::InvalidRefHandle();
	TESObjectCELL* parentCell = refObj->parentCell;
	TESWorldSpace* worldspace = refObj->GetWorldSpace();

	long double zRotationMod = ((long double)zRotation) / 180.0l * PI;

	float cos1 = cos(-zRotationMod);
	float sin1 = sin(-zRotationMod);

	float relativeX;
	float relativeY;

	float newX;
	float newY;

	TESObjectREFR* obj;
	for (UInt32 i = 0; i < selectedObjs.Length(); i++) {
		selectedObjs.Get(&obj, i);
		if (obj) {
			//Get relative Position
			relativeX = obj->data.location.x - originX;
			relativeY = obj->data.location.y - originY;

			//Apply Rotation
			newX = relativeX * cos1 - relativeY * sin1;
			newY = relativeY * cos1 + relativeX * sin1;

			//Get Absolute Position
			newX += originX;
			newY += originY;

			//Set object's new angle
			NiPoint3 newRot;
			newRot.x = obj->data.angle.x;
			newRot.y = obj->data.angle.y;
			newRot.z = obj->data.angle.z + zRotationMod;

			//Set object's new position
			NiPoint3 newPos;
			newPos.x = newX;
			newPos.y = newY;
			newPos.z = obj->data.location.z;

			MoveRefrToPosition(obj, &nullHandle, parentCell, worldspace, &newPos, &newRot);
			// Call again to fix jitter.
			MoveRefrToPosition(obj, &nullHandle, parentCell, worldspace, &newPos, &newRot);
		}
	}
}

//Move all selected objects by the given amounts along the x, y, and z axises.
void MoveSelection(StaticFunctionTag* base, TESObjectREFR* refObj, const float xMovement, const float yMovement, const float zMovement) {
	if (!refObj) {
		return;
	}
	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(nullptr, refObj);

	UInt32 nullHandle = Clipboard::EngineAPI::InvalidRefHandle();
	TESObjectCELL* parentCell = refObj->parentCell;
	TESWorldSpace* worldspace = refObj->GetWorldSpace();

	float cos1 = cos(refObj->data.angle.z);
	float sin1 = sin(refObj->data.angle.z);
	float cos2 = cos(-refObj->data.angle.z);
	float sin2 = sin(-refObj->data.angle.z);

	float relativeX;
	float relativeY;

	float newX;
	float newY;
	float newZ;

	TESObjectREFR* obj;
	for (UInt32 i = 0; i < selectedObjs.Length(); i++) {
		selectedObjs.Get(&obj, i);
		if (obj) {
			//Get relative Position
			relativeX = obj->data.location.x - refObj->data.location.x;
			relativeY = obj->data.location.y - refObj->data.location.y;

			//Remove Rotation
			newX = relativeX * cos1 - relativeY * sin1;
			newY = relativeY * cos1 + relativeX * sin1;

			//Apply movement amount
			newX += xMovement;
			newY += yMovement;
			newZ = obj->data.location.z + zMovement;

			//Remove Rotation
			relativeX = newX * cos2 - newY * sin2;
			relativeY = newY * cos2 + newX * sin2;

			//Get Absolute Position
			newX = relativeX + refObj->data.location.x;
			newY = relativeY + refObj->data.location.y;

			//Set object's new position
			NiPoint3 newPos;
			newPos.x = newX;
			newPos.y = newY;
			newPos.z = newZ;
			MoveRefrToPosition(obj, &nullHandle, parentCell, worldspace, &newPos, &obj->data.angle);
			//Call again to fix jitter.
			MoveRefrToPosition(obj, &nullHandle, parentCell, worldspace, &newPos, &obj->data.angle);
		}
	}
};

//Get an array of all wires between the given objects.
VMArray<TESObjectREFR*> GetSelectedWires(const VMArray<TESObjectREFR*>& objs,
	const Clipboard::Selection::FirstReferenceRows& selectedRows) {
	const auto* splineDefault = GetDefaultForm<BGSBendableSpline>("WorkshopSplineObject");
	const UInt32 splineFormId = splineDefault ? splineDefault->formID : 0;
	VMArray<TESObjectREFR*> selectedWires;
	TESObjectREFR* obj;
	for (UInt32 i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		if (obj) {
			ExtraDataList* extraDataList = obj->extraList.get();
			if (extraDataList) {
				{
					ExtraPowerLinks* powerLinks = extraDataList->GetByType<ExtraPowerLinks>();
					if (powerLinks) {
						for (const auto& connection : powerLinks->powerLinks) {
							TESForm* form = LookupFormByID(connection.formID);
							if (form) {
								TESObjectREFR* wireRef = DYNAMIC_CAST(form, TESForm, TESObjectREFR);
								if (wireRef && wireRef->data.objectReference && wireRef->data.objectReference->formID == splineFormId) {
									//We Found a Wire!
									ExtraPowerLinks* wirePowerLinks = wireRef->extraList ?
										wireRef->extraList->GetByType<ExtraPowerLinks>() : nullptr;
									if (wirePowerLinks && wirePowerLinks->powerLinks.size() == 2) {
										UInt32 formID = wirePowerLinks->powerLinks[0].formID;
										if (obj->formID == formID) { //Check if we are at the starting end.
											formID = wirePowerLinks->powerLinks[1].formID;
											if (selectedRows.Find(formID) >= 0)
												selectedWires.Push(&wireRef);
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	return selectedWires;
}

VMArray<TESObjectREFR*> GetSelectedWires(const VMArray<TESObjectREFR*>& objs)
{
	return GetSelectedWires(objs, GetReferenceRows(objs));
}

//Count the number of wires between selected objects.
UInt32 GetSelectionWireCount(StaticFunctionTag* base, TESObjectREFR* refObj) {
	VMArray<TESObjectREFR*> objs = GetSelectedObjectReferences(base, refObj);
	if (objs.Length() <= 1) {
		return 0;
	}
	VMArray<TESObjectREFR*> wires = GetSelectedWires(objs);
	return wires.Length();
}

//Get the index of a given form's source plugin in the given list of plugins.
int GetSelectedPluginIndex(const TESForm* form, const VMArray<BSFixedString>& plugins) {

	BSFixedString plugin;
	BSFixedString pluginName = GetPluginName(form);
	for (UInt32 plgIndex = 0; plgIndex < plugins.Length(); plgIndex++) {
		plugins.Get(&plugin, plgIndex);
		if (plugin == pluginName) {
			return static_cast<int>(plgIndex);
		}
	}
	return -1;
}

//Get an array of all plugins with forms in the given list.
//   Includes the plugin that sources the workshop these objects are attached to.
VMArray<BSFixedString> GetPlugins(TESObjectREFR* refObj, const VMArray<TESObjectREFR*>& objs) {
	VMArray<BSFixedString> pluginsArray;
	if (!refObj) {
		return pluginsArray;
	}

	TESObjectREFR* obj = (TESObjectREFR*)GetLinkedRef_Native(refObj, workshopItemKeyword);
	auto pluginName = GetPluginName(obj);
	pluginsArray.Push(&pluginName);

	const int referencePluginIndex = refObj->parentCell ? GetSelectedPluginIndex(refObj->parentCell, pluginsArray) : -1;
	if (refObj->parentCell && referencePluginIndex < 0) {
		pluginName = GetPluginName(refObj->parentCell);
		pluginsArray.Push(&pluginName);
	}

	for (UInt32 i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		const TESForm* baseForm = GetBaseForm(obj);
		const int objectPluginIndex = GetSelectedPluginIndex(baseForm, pluginsArray);
		if (objectPluginIndex < 0) {
			pluginName = GetPluginName(baseForm);
			pluginsArray.Push(&pluginName);
		}
	}

	return pluginsArray;
}

VMArray<BSFixedString> GetSelectedPlugins_(StaticFunctionTag* base, TESObjectREFR* refObj) {
	return GetPlugins(refObj, GetSelectedObjectReferences(base, refObj));
}

VMArray<BSFixedString> GetPlugins_(StaticFunctionTag* base, TESObjectREFR* refObj, VMArray<TESObjectREFR*> objs) {
	return GetPlugins(refObj, objs);
}

template <bool IncludeScales = true>
Clipboard::SelectionGeometry::Geometry MeasureSelectedGeometry(const VMArray<TESObjectREFR*>& selectedObjs) {
	return Clipboard::SelectionGeometry::Measure<IncludeScales>(selectedObjs,
		[](const TESObjectREFR* obj) {
			return Clipboard::SelectionGeometry::Point{
				obj->data.location.x, obj->data.location.y, obj->data.location.z };
		},
		[](const TESObjectREFR* obj) { return GetScale(obj); });
}

// A movement query needs only the bounds midpoint. Keep the full public details
// query below for callers that actually display wire/plugin counts and scales.
VMArray<float> GetSelectionCenter(StaticFunctionTag* base, TESObjectREFR* refObj) {
	const auto selectedObjs = GetSelectedObjectReferences(base, refObj);
	const auto center = MeasureSelectedGeometry<false>(selectedObjs).Center();
	return { center.x, center.y, center.z };
}

// Calculates position, area, scale, and count data for the set of selected objects.
SelectionDetails GetSelectionDetails(StaticFunctionTag* base, TESObjectREFR* refObj) {
	const auto selectedObjs = GetSelectedObjectReferences(base, refObj);
	// Preserve the existing count queries and their order for the public result.
	const UInt32 wireCount = selectedObjs.empty() ? 0 : GetSelectedWires(selectedObjs).Length();
	const UInt32 pluginCount = selectedObjs.empty() ? 0 : GetPlugins(refObj, selectedObjs).Length();
	const auto geometry = MeasureSelectedGeometry(selectedObjs);
	const auto area = geometry.Area();
	const auto center = geometry.Center();

	SelectionDetails details;
	details.SetNone(false);
	details.Set<UInt32>("objectCount", selectedObjs.Length());
	details.Set<UInt32>("wireCount", wireCount);
	details.Set<UInt32>("pluginCount", pluginCount);
	details.Set<float>("areaX", area.x);
	details.Set<float>("areaY", area.y);
	details.Set<float>("areaZ", area.z);
	details.Set<float>("centerX", center.x);
	details.Set<float>("centerY", center.y);
	details.Set<float>("centerZ", center.z);
	details.Set<float>("minimumScale", geometry.minimumScale);
	details.Set<float>("maximumScale", geometry.maximumScale);
	details.Set<float>("averageScale", geometry.averageScale);
	return details;
}

//Writes the general information section of a clipboard pattern.
void WriteSelectedGeneralInformation(std::ostream& patternFileStream, BSFixedString patternName, BSFixedString characterName, TESObjectREFR* workshopRef, int wireCount, int objectCount, const VMArray<BSFixedString>& plugins) {
	UInt32 pluginFormId = workshopRef->formID & (UInt32)0xFFFFFF;
	int pluginIndex = GetSelectedPluginIndex(workshopRef, plugins);

	patternFileStream << "[general]\n";
	patternFileStream << "pattern_name=" << patternName.c_str() << "\n";
	patternFileStream << "copied_on=" << GetCurrentDateTime() << "\n";
	patternFileStream << "character=" << characterName.c_str() << "\n";
	patternFileStream << "workshop_id=" << std::to_string(pluginFormId) << "\n";
	patternFileStream << "workshop_plugin=" << std::to_string(pluginIndex) << "\n";
	patternFileStream << "plugin_count=" << plugins.Length() << "\n";
	patternFileStream << "wire_count=" << wireCount << "\n";
	patternFileStream << "object_count=" << objectCount << "\n";
	patternFileStream << "clipboard_version=" << pluginVersionString << std::endl;
}

//Writes the reference information section of a clipboard pattern.
void WriteSelectedReferenceInformation(std::ostream& patternFileStream, TESObjectREFR* referenceObject, const VMArray<BSFixedString>& plugins) {

	UInt32 cellFormId = GetLowerFormId(referenceObject->parentCell);
	int pluginIndex = GetSelectedPluginIndex(referenceObject->parentCell, plugins);

	patternFileStream << "[reference]\n";
	patternFileStream << "cell_id=" << cellFormId << "\n";
	patternFileStream << "cell_plugin=" << pluginIndex << "\n";
	patternFileStream << "position_x=" << ToString(referenceObject->data.location.x) << "\n";
	patternFileStream << "position_y=" << ToString(referenceObject->data.location.y) << "\n";
	patternFileStream << "position_z=" << ToString(referenceObject->data.location.z) << "\n";
	patternFileStream << "angle_x=" << ToString(((long double)referenceObject->data.angle.x) / PI * 180.0) << "\n";
	patternFileStream << "angle_y=" << ToString(((long double)referenceObject->data.angle.y) / PI * 180.0) << "\n";
	patternFileStream << "angle_z=" << ToString(((long double)referenceObject->data.angle.z) / PI * 180.0) << std::endl;
}

//Writes the plugins section of a clipboard pattern.
void WriteSelectedPlugins(std::ostream& patternFileStream, const VMArray<BSFixedString>& selectedPlugins) {

	patternFileStream << "[plugins]";
	BSFixedString pluginName;
	for (UInt32 i = 0; i < selectedPlugins.Length(); i++) {
		selectedPlugins.Get(&pluginName, i);
		patternFileStream << "\n" << i << "=" << pluginName.c_str();
	}
	patternFileStream << std::endl;
}

//Writes the wires section of a clipboard pattern.
void WriteSelectedWires(std::ostream& patternFileStream, const VMArray<TESObjectREFR*>& selectedWires,
	const Clipboard::Selection::FirstReferenceRows& selectedRows) {

	if (selectedWires.Length() > 0) {
		patternFileStream << "[wires]";
		TESObjectREFR* wire;
		UInt64 attachmentFormId1 = 0;
		UInt64 attachmentFormId2 = 0;
		int attachmentIndex1;
		int attachmentIndex2;
		for (UInt32 i = 0; i < selectedWires.Length(); i++) {
			selectedWires.Get(&wire, i);
			if (wire) {
				ExtraPowerLinks* wirePowerLinks = wire->extraList ? wire->extraList->GetByType<ExtraPowerLinks>() : nullptr;
				if (!wirePowerLinks || wirePowerLinks->powerLinks.size() != 2) {
					patternFileStream << "\n" << i << "=" << 0 << "|" << 0;
					continue;
				}
				attachmentFormId1 = wirePowerLinks->powerLinks[0].formID;
				attachmentFormId2 = wirePowerLinks->powerLinks[1].formID;
				attachmentIndex1 = selectedRows.Find(attachmentFormId1);
				attachmentIndex2 = selectedRows.Find(attachmentFormId2);
				patternFileStream << "\n" << i << "=" << attachmentIndex1 << "|" << attachmentIndex2;
			}
			else {
				patternFileStream << "\n" << i << "=" << 0 << "|" << 0;
			}
		}
		patternFileStream << std::endl;
	}
}

//Writes the objects section of a clipboard pattern.
void WriteSelectedObjects(std::ostream& patternFileStream, TESObjectREFR* referenceObject, const VMArray<TESObjectREFR*>& selectedObjects, const VMArray<BSFixedString>& plugins) {

	TESObjectREFR* obj = nullptr;

	float selectionReferenceCos = cos(referenceObject->data.angle.z);
	float selectionReferenceSin = sin(referenceObject->data.angle.z);

	patternFileStream << "[objects]\n";
	for (UInt32 objIndex = 0; objIndex < selectedObjects.Length(); objIndex++) {
		selectedObjects.Get(&obj, objIndex);
		if (obj) {
			const TESForm* baseForm = GetBaseForm(obj);
			int pluginIndex = GetSelectedPluginIndex(baseForm, plugins);

			float relativeX = obj->data.location.x - referenceObject->data.location.x;
			float relativeY = obj->data.location.y - referenceObject->data.location.y;
			float relativeZ = obj->data.location.z - referenceObject->data.location.z;

			long double relativeAngleX = ((long double)obj->data.angle.x) / PI * 180.0;
			long double relativeAngleY = ((long double)obj->data.angle.y) / PI * 180.0;
			long double relativeAngleZ = ((long double)(obj->data.angle.z - referenceObject->data.angle.z)) / PI * 180.0;

			float finalX = relativeX * selectionReferenceCos - relativeY * selectionReferenceSin;
			float finalY = relativeY * selectionReferenceCos + relativeX * selectionReferenceSin;

			UInt32 pluginFormId = GetLowerFormId(baseForm);

			patternFileStream << objIndex << "=";
			patternFileStream << pluginIndex << "|";
			patternFileStream << pluginFormId << "|";
			patternFileStream << ToString(GetScale(obj)) << "|";
			patternFileStream << ToString(finalX) << "|";
			patternFileStream << ToString(finalY) << "|";
			patternFileStream << ToString(relativeZ) << "|";
			patternFileStream << ToString(relativeAngleX) << "|";
			patternFileStream << ToString(relativeAngleY) << "|";
			patternFileStream << ToString(relativeAngleZ) << "\n";
		}
		else {

			int pluginIndex = GetSelectedPluginIndex(GetBaseForm(PlayerCharacter::GetSingleton()), plugins);

			patternFileStream << objIndex << "=";
			patternFileStream << pluginIndex << "|";
			patternFileStream << BOTTLECAP_FORM_ID << "|";
			patternFileStream << "1|0|0|0|0|0|0";
		}
	}
	patternFileStream << std::endl;
}

//Reads all values from a given section of a clipboard pattern in the given slot.
VMArray<BSFixedString> ReadPatternSectionValues(UInt32 slot, BSFixedString section) {
	VMArray<BSFixedString> result;
	std::string configPath = GetPatternFilePath(slot);

	std::string sectionLine(section);
	sectionLine = "[" + sectionLine + "]";
	bool inSection = false;

	std::ifstream inFile;
	std::string line;
	BSFixedString lineFixed;
	std::string::size_type valueIndex;
	inFile.open(configPath.c_str());
	if (!inFile) {
		return result;
	}
	while (std::getline(inFile, line)) {
		if (!line.empty()) {
			if (line.at(0) == '[' && line.back() == ']') {
				if (inSection)
					break;

				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found Section: " + line));
				inSection = iequals(sectionLine, line);
			}
			else if (inSection && line.length() > 0) {
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found Line: " + line));
				valueIndex = line.find('=');
				if (valueIndex != std::string::npos && valueIndex > 0) {
					line = line.substr(valueIndex + 1);
					trim(line);
					CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found Value: " + line));
					lineFixed = line.c_str();
					result.Push(&lineFixed);
				}
			}
		}
	}
	inFile.close();
	return result;
}

//Reads all keys from a given section of a clipboard pattern in the given slot.
VMArray<BSFixedString> ReadPatternSectionKeys(UInt32 slot, BSFixedString section) {
	VMArray<BSFixedString> result;
	std::string configPath = GetPatternFilePath(slot);

	std::string sectionLine(section);
	sectionLine = "[" + sectionLine + "]";
	bool inSection = false;

	std::ifstream inFile;
	std::string line;
	BSFixedString lineFixed;
	std::string::size_type valueIndex;
	inFile.open(configPath.c_str());
	if (!inFile) {
		return result;
	}
	while (std::getline(inFile, line)) {
		if (line.empty()) {
			continue;
		}
		if (line.at(0) == '[' && line.back() == ']') {
			if (inSection)
				break;

			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found Section: " + line));
			inSection = iequals(sectionLine, line);
		}
		else if (inSection) {

			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found Line: " + line));
			valueIndex = line.find('=');
			if (valueIndex == std::string::npos || valueIndex == 0) {
				continue;
			}
			line = line.substr(0, valueIndex);
			trim(line);

			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found Key: " + line));
			lineFixed = line.c_str();
			result.Push(&lineFixed);
		}
	}
	inFile.close();
	return result;
}

BSFixedString ReadPatternSectionValue_(StaticFunctionTag* base, UInt32 slot, BSFixedString section, BSFixedString key) {
	return ReadPatternSectionValue(slot, section, key);
}

VMArray<BSFixedString> ReadPatternSectionValues_(StaticFunctionTag* base, UInt32 slot, BSFixedString section) {
	return ReadPatternSectionValues(slot, section);
}

VMArray<BSFixedString> ReadPatternSectionKeys_(StaticFunctionTag* base, UInt32 slot, BSFixedString section) {
	return ReadPatternSectionKeys(slot, section);
}

//Reads all object data from the clipboard pattern at the given slot.
VMArray<PatternObjectEntry> GetPatternObjects(StaticFunctionTag* base, UInt32 slot) {
	VMArray<PatternObjectEntry> objects;
	BSFixedString objectLine;
	std::string elementString;
	VMArray<BSFixedString> objectLines = ReadPatternSectionValues(slot, "objects");
	for (UInt32 i = 0; i < objectLines.Length(); i++) {
		PatternObjectEntry entry;
		entry.SetNone(false);
		objectLines.Get(&objectLine, i);
		std::string line = objectLine.c_str();
		CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("New Object Line: " + line));
		UInt32 elementIndex = 0;
		std::string::size_type startIndex = 0;
		if (line.length() == 0) {
			//Found an empty line, time to escape.
			break;
		}
		while (true) {
			const auto endIndex = line.find('|', startIndex);
			elementString = endIndex == std::string::npos ?
				line.substr(startIndex) : line.substr(startIndex, endIndex - startIndex);

			if (elementIndex == 0) {
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found pluginIndex: " + elementString));
				entry.Set<UInt32>("pluginIndex", (UInt32)ToInt(elementString));
			}
			else if (elementIndex == 1) {
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found formId: " + elementString));
				UInt64 formId = ToInt64(elementString);
				entry.Set<UInt32>("formId", (UInt32)(formId & 0xFFFFFF));
			}
			else if (elementIndex == 2) {
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found scale: " + elementString));
				entry.Set<float>("scale", ToFloat(elementString));
			}
			else if (elementIndex == 3) {
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found positionX: " + elementString + ", " + std::to_string(ToFloat(elementString))));
				entry.Set<float>("positionX", ToFloat(elementString));
			}
			else if (elementIndex == 4) {
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found positionY: " + elementString + ", " + std::to_string(ToFloat(elementString))));
				entry.Set<float>("positionY", ToFloat(elementString));
			}
			else if (elementIndex == 5) {
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found positionZ: " + elementString));
				entry.Set<float>("positionZ", ToFloat(elementString));
			}
			else if (elementIndex == 6) {
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found angleX: " + elementString));
				entry.Set<float>("angleX", Clipboard::PatternNumbers::Radians(elementString, PI));
			}
			else if (elementIndex == 7) {
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found angleY: " + elementString));
				entry.Set<float>("angleY", Clipboard::PatternNumbers::Radians(elementString, PI));
			}
			else if (elementIndex == 8) {
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found angleZ: " + elementString));
				entry.Set<float>("angleZ", Clipboard::PatternNumbers::Radians(elementString, PI));
			}

			elementIndex++;
			if (endIndex == std::string::npos) {
				break;
			}
			startIndex = endIndex + 1;
		}
		objects.Push(&entry);
	}

	return objects;
}

//Reads all wire data from the clipboard pattern at the given slot.
VMArray<PatternWireEntry> GetPatternWires(StaticFunctionTag* base, UInt32 slot) {
	VMArray<PatternWireEntry> wires;
	BSFixedString wireLine;
	std::string elementString;
	VMArray<BSFixedString> wireLines = ReadPatternSectionValues(slot, "wires");
	for (UInt32 i = 0; i < wireLines.Length(); i++) {
		PatternWireEntry entry;
		entry.SetNone(false);
		VMArray<BSFixedString> object;
		wireLines.Get(&wireLine, i);
		std::string line = wireLine.c_str();
		CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("New Wire Line: " + line));
		UInt32 elementIndex = 0;
		std::string::size_type startIndex = 0;
		if (line.length() == 0) {
			//Found an empty line, time to escape.
			break;
		}
		while (true) {
			const auto endIndex = line.find('|', startIndex);
			elementString = endIndex == std::string::npos ?
				line.substr(startIndex) : line.substr(startIndex, endIndex - startIndex);

			if (elementIndex == 0) {
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found attachmentIndex1: " + elementString));
				entry.Set<UInt32>("attachmentIndex1", ToInt(elementString));
			}
			else if (elementIndex == 1) {
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found attachmentIndex2: " + elementString));
				entry.Set<UInt32>("attachmentIndex2", ToInt(elementString));
			}

			elementIndex++;
			if (endIndex == std::string::npos) {
				break;
			}
			startIndex = endIndex + 1;
		}
		wires.Push(&entry);
	}

	return wires;
}

//Reads all plugin data from the clipboard pattern at the given slot.
VMArray<BSFixedString> GetPatternPlugins(StaticFunctionTag* base, UInt32 slot)
{
	return ReadPatternSectionValues(slot, "plugins");
}

//Reads the general information for the clipboard pattern in the given slot.
PatternGeneralEntry GetPatternGeneralInformation(StaticFunctionTag* base, UInt32 slot)
{
	PatternGeneralEntry entry;
	entry.SetNone(false);
	VMArray<BSFixedString> keys = ReadPatternSectionKeys(slot, "general");
	VMArray<BSFixedString> values = ReadPatternSectionValues(slot, "general");
	BSFixedString elementKey;
	BSFixedString elementValue;
	for (UInt32 i = 0; i < keys.Length(); i++) {
		keys.Get(&elementKey, i);
		values.Get(&elementValue, i);
		std::string elementValueString = elementValue.c_str();
		if (elementKey == "pattern_name") {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found patternName: " + elementValueString));
			entry.Set("patternName", (BSFixedString)elementValue);
		}
		else if (elementKey == "character") {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found characterName: " + elementValueString));
			entry.Set("characterName", (BSFixedString)elementValue);
		}
		else if (elementKey == "workshop_id") {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found workshopId: " + elementValueString));
			UInt64 workshopId = ToInt64(elementValue.c_str());
			entry.Set<UInt32>("workshopId", (UInt32)(workshopId & 0xFFFFFF));
		}
		else if (elementKey == "workshop_plugin") {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found workshopPlugin: " + elementValueString));
			entry.Set<UInt32>("workshopPlugin", ToInt(elementValue.c_str()));
		}
		else if (elementKey == "plugin_count") {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found pluginCount: " + elementValueString));
			entry.Set<UInt32>("pluginCount", ToInt(elementValue.c_str()));
		}
		else if (elementKey == "wire_count") {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found wireCount: " + elementValueString));
			entry.Set<UInt32>("wireCount", ToInt(elementValue.c_str()));
		}
		else if (elementKey == "object_count") {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found objectCount: " + elementValueString));
			entry.Set<UInt32>("objectCount", ToInt(elementValue.c_str()));
		}
	}

	return entry;
}

//Get the number of populated clipboard patterns in the given range of slots.
UInt32 GetPatternCount(StaticFunctionTag* base, UInt32 firstSlot, UInt32 lastSlot) {
	UInt32 count = 0;
	for (UInt32 i = firstSlot; i <= lastSlot; i++) {
		PatternGeneralEntry generalEntry = GetPatternGeneralInformation(base, i);
		UInt32 objectCount = 0;
		generalEntry.Get<UInt32>("objectCount", &objectCount);

		if (objectCount > 0) {
			count++;
		}
		if (i == std::numeric_limits<UInt32>::max()) {
			break;
		}
	}
	return count;
}

//Reads the reference information for the clipboard pattern in the given slot.
std::optional<PatternReferenceEntry> GetPatternReferenceInformation(StaticFunctionTag* base, UInt32 slot) {
	PatternReferenceEntry entry;
	entry.SetNone(false);
	VMArray<BSFixedString> keys = ReadPatternSectionKeys(slot, "reference");
	VMArray<BSFixedString> values = ReadPatternSectionValues(slot, "reference");
	BSFixedString elementKey;
	BSFixedString elementValue;
	bool hasCellId = false;
	bool hasCellPlugin = false;
	bool hasPositionX = false;
	bool hasPositionY = false;
	bool hasPositionZ = false;
	bool hasAngleX = false;
	bool hasAngleY = false;
	bool hasAngleZ = false;
	for (UInt32 i = 0; i < keys.Length(); i++) {
		if (!keys.Get(&elementKey, i) || !values.Get(&elementValue, i)) {
			logger::error("Pattern slot {} reference row {} has misaligned key/value data", slot, i);
			continue;
		}
		std::string elementValueString = elementValue.c_str();

		if (elementKey == "cell_id") {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found cellId: " + elementValueString));
			UInt32 parsed = 0;
			const auto* begin = elementValueString.data();
			const auto* end = begin + elementValueString.size();
			const auto [next, error] = std::from_chars(begin, end, parsed, 10);
			hasCellId = error == std::errc{} && next == end && entry.Set("cellId", parsed);
		}
		else if (elementKey == "cell_plugin") {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found cellPlugin: " + elementValueString));
			UInt32 parsed = 0;
			const auto* begin = elementValueString.data();
			const auto* end = begin + elementValueString.size();
			const auto [next, error] = std::from_chars(begin, end, parsed, 10);
			hasCellPlugin = error == std::errc{} && next == end && entry.Set("cellPlugin", parsed);
		}
		else if (elementKey == "position_x") {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found positionX: " + elementValueString));
			float parsed = 0.0F;
			const auto* begin = elementValueString.data();
			const auto* end = begin + elementValueString.size();
			const auto [next, error] = std::from_chars(begin, end, parsed, std::chars_format::general);
			hasPositionX = error == std::errc{} && next == end && std::isfinite(parsed) && entry.Set("positionX", parsed);
		}
		else if (elementKey == "position_y") {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found positionY: " + elementValueString));
			float parsed = 0.0F;
			const auto* begin = elementValueString.data();
			const auto* end = begin + elementValueString.size();
			const auto [next, error] = std::from_chars(begin, end, parsed, std::chars_format::general);
			hasPositionY = error == std::errc{} && next == end && std::isfinite(parsed) && entry.Set("positionY", parsed);
		}
		else if (elementKey == "position_z") {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found positionZ: " + elementValueString));
			float parsed = 0.0F;
			const auto* begin = elementValueString.data();
			const auto* end = begin + elementValueString.size();
			const auto [next, error] = std::from_chars(begin, end, parsed, std::chars_format::general);
			hasPositionZ = error == std::errc{} && next == end && std::isfinite(parsed) && entry.Set("positionZ", parsed);
		}
		else if (elementKey == "angle_x") {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found angleX: " + elementValueString));
			double parsed = 0.0;
			const auto* begin = elementValueString.data();
			const auto* end = begin + elementValueString.size();
			const auto [next, error] = std::from_chars(begin, end, parsed, std::chars_format::general);
			hasAngleX = error == std::errc{} && next == end && std::isfinite(parsed) &&
				entry.Set("angleX", static_cast<float>(static_cast<long double>(parsed) / 180.0L * PI));
		}
		else if (elementKey == "angle_y") {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found angleY: " + elementValueString));
			double parsed = 0.0;
			const auto* begin = elementValueString.data();
			const auto* end = begin + elementValueString.size();
			const auto [next, error] = std::from_chars(begin, end, parsed, std::chars_format::general);
			hasAngleY = error == std::errc{} && next == end && std::isfinite(parsed) &&
				entry.Set("angleY", static_cast<float>(static_cast<long double>(parsed) / 180.0L * PI));
		}
		else if (elementKey == "angle_z") {
			CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Found angleZ: " + elementValueString));
			double parsed = 0.0;
			const auto* begin = elementValueString.data();
			const auto* end = begin + elementValueString.size();
			const auto [next, error] = std::from_chars(begin, end, parsed, std::chars_format::general);
			hasAngleZ = error == std::errc{} && next == end && std::isfinite(parsed) &&
				entry.Set("angleZ", static_cast<float>(static_cast<long double>(parsed) / 180.0L * PI));
		}
	}
	const bool complete =
		hasCellId && hasCellPlugin &&
		hasPositionX && hasPositionY && hasPositionZ &&
		hasAngleX && hasAngleY && hasAngleZ;
	CLIPBOARD_DEBUG_LOG(
		UInt32 cellId = 0;
		UInt32 cellPlugin = 0;
		float positionX = 0.0F;
		float positionY = 0.0F;
		float positionZ = 0.0F;
		float angleX = 0.0F;
		float angleY = 0.0F;
		float angleZ = 0.0F;
		entry.Get("cellId", &cellId);
		entry.Get("cellPlugin", &cellPlugin);
		entry.Get("positionX", &positionX);
		entry.Get("positionY", &positionY);
		entry.Get("positionZ", &positionZ);
		entry.Get("angleX", &angleX);
		entry.Get("angleY", &angleY);
		entry.Get("angleZ", &angleZ);
		logger::info(
		"Pattern slot {} reference {}: cell {} row {}, position ({:.3f}, {:.3f}, {:.3f}), angle-radians ({:.6f}, {:.6f}, {:.6f})",
		slot,
		complete ? "parsed" : "incomplete",
		cellId,
		cellPlugin,
		positionX,
		positionY,
		positionZ,
		angleX,
		angleY,
		angleZ));
	if (!complete) {
		logger::error("Pattern slot {} reference section is incomplete; returning None to Papyrus", slot);
		return std::nullopt;
	}
	return entry;
}

//Get the first constructible object that builds the given formid. WMK Edited to fix C4703 Error
BGSConstructibleObject* GetConstructibleObjectByCreatedObject_(StaticFunctionTag *base, TESForm* baseObject) {
	auto* constructible = FindConstructibleObjectByCreatedObject(baseObject, false);
	return constructible;
}

//Get an array of all constructible objects.
//TODO: Confirm unused, remove.
VMArray<BGSConstructibleObject*> GetAllConstructibleObjects_(StaticFunctionTag *base) {
	VMArray<BGSConstructibleObject*> result;
	auto* dataHandler = TESDataHandler::GetSingleton();
	if (!dataHandler) {
		return result;
	}
	for (auto* constructible : dataHandler->GetFormArray<BGSConstructibleObject>()) {
		result.Push(&constructible);
	}
	return result;
}

//Get a full form id from a pattern object, using a bottlecap as the fallback object.
UInt32 ToFormID(PatternObjectEntry objectEntry, VMArray<BSFixedString> plugins) {
	//Retreive and process form identifiers.
	UInt32 formId = 0;
	objectEntry.Get<UInt32>("formId", &formId);

	//Retreive and process plugin.
	SInt32 pluginIndex = -1;
	objectEntry.Get<SInt32>("pluginIndex", &pluginIndex);
	BSFixedString plugin;
	if (pluginIndex >= 0 && static_cast<UInt32>(pluginIndex) < plugins.Length()) {
		plugins.Get(&plugin, pluginIndex);
	}
	else {
		AppendToLog("Invalid plugin Index, use a bottlecap.");
		return BOTTLECAP_FORM_ID;
	}

	return GetFullFormId(formId, plugin);
}

// Ordinary (non-tasklet) native reads use the stock inventory counter. No
// balances, references or source topology are retained between calls.
std::int32_t CountComponentSource(StaticFunctionTag*, TESForm* componentForm, TESObjectREFR* sourceRef)
{
	if (!componentForm || !sourceRef || sourceRef->IsDeleted()) { return -1; }
	const auto* sourceBase = sourceRef->data.objectReference;
	if (!sourceBase || (!sourceBase->Is(ENUM_FORM_ID::kCONT) && !sourceBase->Is(ENUM_FORM_ID::kNPC_))) {
		return -1;
	}
	std::uint32_t count = 0;
	if (!sourceRef->GetItemCount(count, componentForm, componentForm->Is(ENUM_FORM_ID::kCMPO))) {
		logger::warn("Component inventory query failed for source {:08X}, form {:08X}; count unavailable",
			sourceRef->formID, componentForm->formID);
		return -1;
	}
	const auto usable = Clipboard::ComponentInventory::UsableSourceCount(count);
	if (usable < 0) {
		logger::warn("Component inventory count {} exceeds Papyrus payment range for source {:08X}, form {:08X}; source excluded",
			count, sourceRef->formID, componentForm->formID);
	}
	return usable;
}

std::int32_t CountComponentSources(StaticFunctionTag*, TESForm* componentForm,
	VMArray<TESObjectREFR*> sources, std::int32_t startIndex)
{
	if (!componentForm) { return 0; }
	return Clipboard::ComponentInventory::SumBatch(sources, startIndex,
		[componentForm](TESObjectREFR* source) { return CountComponentSource(nullptr, componentForm, source); });
}

void ReportComponentCostTiming(StaticFunctionTag*, BSFixedString phase, float elapsedSeconds,
	std::int32_t sourceCount, std::int32_t componentCount)
{
	if (!Clipboard::Logging::Enabled() || !std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0F) { return; }
	logger::info("Component cost timing: phase={} seconds={:.6f} sources={} components={}",
		phase.c_str(), elapsedSeconds, sourceCount, componentCount);
}

//Get the full component cost of all objects in the clipboard pattern at the given slot.
VMArray<ComponentEntry> GetPatternComponentCost(StaticFunctionTag* base, UInt32 slot) {
	VMArray<PatternObjectEntry> objects = GetPatternObjects(base, slot);
	VMArray<ComponentEntry> components;
	Clipboard::ComponentCost::RecipeMemo<BGSConstructibleObject> recipeMemo;
	std::map <UInt32, UInt32> countMap;
	std::map <UInt32, BSFixedString> nameMap;

	VMArray<BSFixedString> plugins = GetPatternPlugins(nullptr, slot);
	PatternObjectEntry objectEntry;

	for (UInt32 i = 0; i < objects.Length(); i++) {
		objects.Get(&objectEntry, i);
		UInt32 formId = ToFormID(objectEntry, plugins);
		TESForm* baseForm = LookupFormByID(formId);
		if (baseForm && baseForm->formID != BOTTLECAP_FORM_ID &&
			(IsWorkshop(baseForm) ||
			 istarts_with(GetPluginName(baseForm).c_str(), "Clipboard.") ||
			 IsBaseFormStructurallyBlockedForImport(baseForm) ||
			 IsBaseFormBlockedForTransfer(baseForm, true))) {
			CLIPBOARD_DEBUG_LOG(logger::info(
				"Component cost omitted pattern row {} because base {:08X} is not import-eligible",
				i,
				baseForm->formID));
			continue;
		}

		// Cost and charging follow the exact import-admission decision above.
		// Once a row is admitted, count any available recipe rather than limiting
		// permissive LIGH rows to workshop-bench recipes.
		BGSConstructibleObject* conObj =
			recipeMemo.Find(baseForm ? baseForm->formID : 0, [&] {
				return GetConstructibleObjectByCreatedObject_(base, baseForm);
			});
		if (conObj && conObj->requiredItems) {
			for (const auto& required : *conObj->requiredItems) {
				TESForm* component = required.first;
				const UInt32 count = required.second.i;
				if (!component) {
					continue;
				}
				std::map<UInt32, UInt32>::iterator search = countMap.find(component->formID);
				if (search != countMap.end()) {
					countMap[component->formID] += count;
				}
				else {
					countMap[component->formID] = count;
					nameMap[component->formID] = BSFixedString{ TESFullName::GetFullName(*component) };
				}
			}
		}
	}

	for (std::map<UInt32, BSFixedString>::iterator it = nameMap.begin(); it != nameMap.end(); ++it) {
		ComponentEntry compEntry;
		compEntry.Set<UInt32>("formId", it->first);
		compEntry.Set<BSFixedString>("name", it->second);
		compEntry.Set<UInt32>("count", countMap[it->first]);

		components.Push(&compEntry);
	}

	return components;
}

//Get the full component cost of all objects in the given array.
VMArray<ComponentEntry> GetComponentCost(StaticFunctionTag* base, VMArray<TESObjectREFR*> objs) {

	VMArray<ComponentEntry> components;
	Clipboard::ComponentCost::RecipeMemo<BGSConstructibleObject> recipeMemo;
	std::map <UInt32, UInt32> countMap;
	std::map <UInt32, BSFixedString> nameMap;

	TESObjectREFR* obj;
	for (UInt32 i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		if (!obj || !obj->data.objectReference) {
			continue;
		}
		BGSConstructibleObject* conObj = recipeMemo.Find(obj->data.objectReference->formID, [&] {
			return GetConstructibleObjectByCreatedObject_(nullptr, obj->data.objectReference);
		});
		try {
			if (conObj && conObj->requiredItems) {
				for (const auto& required : *conObj->requiredItems) {
					TESForm* component = required.first;
					const UInt32 count = required.second.i;
					if (!component) {
						continue;
					}
					std::map<UInt32, UInt32>::iterator search = countMap.find(component->formID);
					if (search != countMap.end()) {
						countMap[component->formID] += count;
					}
					else {
						countMap[component->formID] = count;
						nameMap[component->formID] = BSFixedString{ TESFullName::GetFullName(*component) };
					}
				}
			}
		}
		catch (...) {
			AppendToLog("ERROR: " + std::to_string(i) + " " +
				std::string(TESFullName::GetFullName(*obj->data.objectReference)) +
				" Constructible Object component failed.");
		}
	}

	for (std::map<UInt32, BSFixedString>::iterator it = nameMap.begin(); it != nameMap.end(); ++it) {
		ComponentEntry compEntry;
		compEntry.Set<UInt32>("formId", it->first);
		compEntry.Set<BSFixedString>("name", it->second);
		compEntry.Set<UInt32>("count", countMap[it->first]);
		components.Push(&compEntry);
	}

	return components;
}

//Get the full component cost of all selected objects.
VMArray<ComponentEntry> GetSelectionComponentCost(StaticFunctionTag* base, TESObjectREFR* refObj) {
	return GetComponentCost(base, GetSelectedObjectReferences(base, refObj));
}

static bool FilterPoolCandidate(const TESObjectREFR* obj, bool automaticTransferPolicy)
{
	return automaticTransferPolicy ?
		FilterAutomaticSelection(nullptr, obj) :
		FilterSelection(nullptr, obj);
}

//Adds all eligible objects in the given cell to the given array of objects. Blocks Duplicates.
void AppendObjectByCell(VMArray<TESObjectREFR*>* objList, TESObjectCELL* cell, bool automaticTransferPolicy,
	Clipboard::Selection::FirstReferenceRows& selectedRows) {
	Clipboard::OptionalFiltering::ScopedScan optionalScan;
	if (!objList || !cell) {
		return;
	}
	for (const auto& reference : cell->references) {
		TESObjectREFR* obj = reference.get();

		if (!FilterPoolCandidate(obj, automaticTransferPolicy)) {
			continue;
		}
		if (!selectedRows.Insert(obj->formID, objList->Length())) {
			continue;
		}

		objList->Push(&obj);
	}
}

static BGSLocation* GetAssignedLocation(const ExtraDataList* extraList)
{
	const auto* extra = extraList ? extraList->GetByType<ExtraLocation>() : nullptr;
	return extra ? extra->location : nullptr;
}

static BGSLocation* GetAssignedCellLocation(const TESObjectCELL* cell)
{
	// CELL XLCN, not GetLocation(): the latter can prefer an encounter-zone
	// location or fall back to the whole worldspace (see the Phase-4 audit).
	return cell ? GetAssignedLocation(cell->extraList.get()) : nullptr;
}

static bool SharesWorkshopSpace(const TESObjectCELL* cell, const TESObjectCELL* workshopCell)
{
	if (!cell || !workshopCell) {
		return false;
	}
	if (cell->IsInterior() || workshopCell->IsInterior()) {
		return cell == workshopCell;
	}
	return workshopCell->worldSpace && cell->worldSpace == workshopCell->worldSpace;
}

// Read-only guard for player, tool and saved source/landing positions. Do not
// infer settlement membership from the tool's link or a persistent parent cell.
bool IsPositionWithinWorkshop(StaticFunctionTag*, TESObjectREFR* workshop, TESObjectCELL* cell, float x, float y, float z)
{
	if (!workshop || workshop->IsDeleted() || !workshop->parentCell || !cell) {
		return false;
	}
	const auto space = [](const TESObjectCELL* value) {
		return Clipboard::SourceMovement::Space{
			value->formID, value->IsInterior(), !value->IsInterior() && value->worldSpace ? value->worldSpace->formID : 0 };
	};
	return Clipboard::SourceMovement::IsWithinWorkshop(space(cell), space(workshop->parentCell), x, y, z,
		[workshop](float px, float py, float pz) {
			return RE::Workshop::IsLocationWithinBuildableArea(*workshop, RE::NiPoint3{ px, py, pz });
		});
}

static TESObjectCELL* GetPhysicalCell(TESObjectREFR* reference)
{
	auto* parent = reference ? reference->parentCell : nullptr;
	if (!parent || parent->IsInterior()) {
		return parent;
	}
	auto* worldspace = parent->worldSpace;
	if (!worldspace || parent != worldspace->persistentCell) {
		return parent;
	}

	// A persistent parent is worldspace-wide storage, not the reference's
	// physical cell. Never use that parent cell's location as membership proof.
	const auto key = Clipboard::WorkshopScope::ExteriorCellKey(
		reference->data.location.x, reference->data.location.y);
	if (key) {
		const auto found = worldspace->cellMap.find(*key);
		if (found != worldspace->cellMap.end() && found->second != parent) {
			return found->second;
		}
	}
	return nullptr;
}

static void AppendWorkshopCellObjects(
	VMArray<TESObjectREFR*>* result,
	const VMArray<TESObjectREFR*>& workshopObjects,
	TESObjectREFR* workshop,
	bool automaticTransferPolicy,
	Clipboard::Selection::FirstReferenceRows& selectedRows)
{
	Clipboard::OptionalFiltering::ScopedScan optionalScan;
	if (!result || !workshop) {
		return;
	}

	const UInt32 workshopXlrl = Clipboard::EngineAPI::GetLocationReferenceID(workshop);
	auto* assignedForm = workshopXlrl ? LookupFormByID(workshopXlrl) : nullptr;
	BGSLocation* workshopLocation = assignedForm && assignedForm->Is(ENUM_FORM_ID::kLCTN) ?
		static_cast<BGSLocation*>(assignedForm) : nullptr;
	if (!workshopLocation) {
		workshopLocation = GetAssignedCellLocation(GetPhysicalCell(workshop));
	}
	const UInt32 targetLocationId = workshopLocation ? workshopLocation->formID : 0;

	// The generic TESDataHandler CELL array is not a complete exterior/persistent
	// reference inventory. Snapshot represented forms under the map's own lock;
	// retain references and release the lock before any engine/filter calls.
	std::vector<NiPointer<TESObjectREFR>> representedReferences;
	{
		const auto [forms, lock] = TESForm::GetAllForms();
		BSAutoReadLock readLock{ lock };
		if (forms) {
			for (const auto& [id, form] : *forms) {
				if (!form) {
					continue;
				}
				if (form->Is(ENUM_FORM_ID::kREFR, ENUM_FORM_ID::kACHR)) {
					auto* reference = static_cast<TESObjectREFR*>(form);
					if (SharesWorkshopSpace(reference->parentCell, workshop->parentCell)) {
						representedReferences.emplace_back(reference);
					}
				}
			}
		}
	}
	std::sort(representedReferences.begin(), representedReferences.end(), [](const auto& left, const auto& right) {
		return left->formID < right->formID;
	});
	std::set<UInt32> evaluated;
	auto appendCandidate = [&](TESObjectREFR* obj, bool suppliedChild) {
		if (!obj || !evaluated.insert(obj->formID).second) {
			return;
		}
		auto* physicalCell = GetPhysicalCell(obj);
		auto* physicalLocation = GetAssignedCellLocation(physicalCell);
		auto* referenceLocation = GetAssignedLocation(obj->extraList.get());
		// Ownership is only a missing-location fallback for actual supplied
		// children; do not make an engine link lookup for every worldspace form.
		auto* owner = suppliedChild && workshopItemKeyword ? GetLinkedRef_Native(obj, workshopItemKeyword) : nullptr;
		const auto membership = Clipboard::WorkshopScope::ResolveMembership(
			SharesWorkshopSpace(obj->parentCell, workshop->parentCell), targetLocationId,
			physicalLocation ? physicalLocation->formID : 0,
			referenceLocation ? referenceLocation->formID : 0,
			suppliedChild && owner == workshop);
		if (membership == Clipboard::WorkshopScope::Membership::kOutside) {
			return;
		}
		const bool accepted = FilterPoolCandidate(obj, automaticTransferPolicy);
		if (!accepted) {
			return;
		}
		if (selectedRows.Insert(obj->formID, result->Length())) {
			result->Push(&obj);
		}
	};

	// Evaluate actual linked children first, not just their parent cells. A
	// created persistent bed/chair/container need not occur in cell->references.
	TESObjectREFR* child = nullptr;
	for (UInt32 index = 0; index < workshopObjects.Length(); ++index) {
		workshopObjects.Get(&child, index);
		appendCandidate(child, true);
	}
	for (const auto& reference : representedReferences) {
		appendCandidate(reference.get(), false);
	}
}

static VMArray<TESObjectREFR*> BuildSelectableObjectPool(
	const VMArray<TESObjectREFR*>& workshopObjects,
	TESObjectREFR* refObj,
	bool automaticTransferPolicy)
{
	Clipboard::OptionalFiltering::ScopedScan optionalScan;
	VMArray<TESObjectREFR*> result;
	if (!refObj) {
		return result;
	}
	Clipboard::Selection::FirstReferenceRows selectedRows(workshopObjects.Length());
	TESObjectREFR* obj = nullptr;

	for (UInt32 i = 0; i < workshopObjects.Length(); i++) {
		workshopObjects.Get(&obj, i);

		if (obj == refObj || !FilterPoolCandidate(obj, automaticTransferPolicy)) {
			continue;
		}

		if (!selectedRows.Insert(obj->formID, result.Length())) {
			continue;
		}

		result.Push(&obj);
	}
	CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Results Size: " + std::to_string(result.Length())));
	// Always discover both sources, independent of strict/permissive policy.
	// Shape/plugin callers narrow this pool; explicit By Cell uses its own
	// cell scope. Manual gun registration shares discovery, not transfer policy.
	if (refObj->parentCell) {
		AppendObjectByCell(&result, refObj->parentCell, automaticTransferPolicy, selectedRows);
	}
	CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Results Size: " + std::to_string(result.Length())));

	TESObjectREFR* workshop = (TESObjectREFR*)GetLinkedRef_Native(refObj, workshopItemKeyword);
	AppendWorkshopCellObjects(
		&result,
		workshopObjects,
		workshop,
		automaticTransferPolicy,
		selectedRows);
	CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Results Size: " + std::to_string(result.Length())));

	return result;
}

//Gets an array of all selectable objects for broad automatic selection modes.
VMArray<TESObjectREFR*> GetSelectableObjectPool(StaticFunctionTag*, VMArray<TESObjectREFR*> workshopObjects, TESObjectREFR* refObj)
{
	return BuildSelectableObjectPool(workshopObjects, refObj, true);
}

//Gets the same candidate sources for explicit gun selection while retaining
// baseline single-reference safeguards plus optional content exclusions.
VMArray<TESObjectREFR*> GetManualSelectableObjectPool(StaticFunctionTag*, VMArray<TESObjectREFR*> workshopObjects, TESObjectREFR* refObj)
{
	return BuildSelectableObjectPool(workshopObjects, refObj, false);
}

//Gets an array of all objects in the given cell.
//TODO: Check if unused, remove.
VMArray<TESObjectREFR*> GetObjectsByCell(StaticFunctionTag* base, TESObjectCELL* cell) {
	VMArray<TESObjectREFR*> result;
	if (!cell) {
		return result;
	}
	for (const auto& reference : cell->references) {
		if (TESObjectREFR* obj = reference.get()) {
			result.Push(&obj);
		}
	}
	return result;
}

// Gets references from every cell associated with the current workshop
// location, including directly linked children and spatially matched persistent
// references. The worldspace persistent cell is never admitted wholesale.
VMArray<TESObjectREFR*> GetObjectsByWorkshopCells(
	StaticFunctionTag*,
	VMArray<TESObjectREFR*> workshopObjects,
	TESObjectREFR* workshop)
{
	VMArray<TESObjectREFR*> result;
	Clipboard::Selection::FirstReferenceRows selectedRows(workshopObjects.Length());
	AppendWorkshopCellObjects(&result, workshopObjects, workshop, false, selectedRows);
	return result;
}

// Compact in native storage, not Papyrus Array.Add's 128-element buffer.
// Only worldspace-persistent storage is range-limited. Ordinary settlement
// cells remain uncapped; same-space/finite-position validity still applies.
// Strict/permissive admission remains in SelectAll/export, unchanged.
VMArray<TESObjectREFR*> FilterObjectsByDistance(
	StaticFunctionTag*, VMArray<TESObjectREFR*> objects, TESObjectREFR* origin, float maximum)
{
	Clipboard::WideSelection::Counts counts;
	const auto requiresLimit = [](TESObjectREFR* object) {
		const auto* cell = object ? object->parentCell : nullptr;
		return cell && Clipboard::WideSelection::UsesPersistentCellLimit(
			cell->IsInterior(), cell, cell->worldSpace ? cell->worldSpace->persistentCell : nullptr);
	};
	const auto result = Clipboard::WideSelection::Filter<VMArray<TESObjectREFR*>>(
		objects, maximum, [origin](TESObjectREFR* object) {
			double distance = -1.0;
			if (origin && object) {
				const auto& from = origin->data.location;
				const auto& to = object->data.location;
				distance = Clipboard::WideSelection::Distance(
					SharesWorkshopSpace(object->parentCell, origin->parentCell),
					{ from.x, from.y, from.z }, { to.x, to.y, to.z });
			}
			return distance;
		}, requiresLimit, counts);
	return result;
}

//Gets an array of all objects in the given list that were sourced by the given plugin.
VMArray<TESObjectREFR*> FilterObjectsByPlugin(StaticFunctionTag* base, VMArray<TESObjectREFR*> selectionPool, BSFixedString pluginName) {
	VMArray<TESObjectREFR*> result;
	TESObjectREFR* obj;
	for (UInt32 i = 0; i < selectionPool.Length(); i++) {
		selectionPool.Get(&obj, i);
		TESForm* sourceForm = obj && obj->data.objectReference ?
			static_cast<TESForm*>(obj->data.objectReference) : static_cast<TESForm*>(obj);
		BSFixedString objPluginName = GetPluginName(sourceForm);
		if (iequals(objPluginName.c_str(), pluginName.c_str())) {
			result.Push(&obj);
		}
	}
	return result;
}

#include "ExistingPowerEndpoints.inl"

//Write the full clipboard pattern.
std::optional<PatternGeneralEntry> WritePatternFile(StaticFunctionTag* base, UInt32 slot, BSFixedString patternName, BSFixedString characterName, TESObjectREFR* referenceObject) {
	// The nullable C++ wrapper keeps the same Papyrus structure return type;
	// unlike LegacyStructure::SetNone, it explicitly packs failure as None.
	constexpr auto failure = std::nullopt;
	try {
		Clipboard::OptionalFiltering::ScopedScan optionalScan;

		if (!referenceObject || !referenceObject->parentCell) {
			return failure;
		}
		// Capture and retain the owner before preparing the replacement snapshot.
		// A missing WorkshopItem link is a recoverable export failure.
		const NiPointer<TESObjectREFR> workshopRef{ workshopItemKeyword ?
			GetLinkedRef_Native(referenceObject, workshopItemKeyword) : nullptr };
		if (!workshopRef || workshopRef->IsDeleted()) {
			logger::warn("WritePatternFile rejected tool {:08X}: no live WorkshopItem owner", referenceObject->formID);
			return failure;
		}
		VMArray<TESObjectREFR*> selectedObjects;
		TESObjectREFR* selectedObject = nullptr;
		const VMArray<TESObjectREFR*> currentSelection = GetSelectedObjectReferences(base, referenceObject);
		for (UInt32 i = 0; i < currentSelection.Length(); ++i) {
			currentSelection.Get(&selectedObject, i);
			// Broad selection already applies the automatic-admission option.
			// Export honors explicit selections even when that option is disabled.
			if (selectedObject && FilterSelection(nullptr, selectedObject) &&
				!IsBaseFormBlockedForTransfer(GetBaseForm(selectedObject), false, true)) {
				selectedObjects.Push(&selectedObject);
			} else if (selectedObject) {
				const TESForm* baseForm = GetBaseForm(selectedObject);
				logger::warn(
					"Export omitted selected reference {:08X} (base {:08X}, source {})",
					selectedObject->formID,
					baseForm ? baseForm->formID : 0,
					baseForm ? GetPluginName(baseForm).c_str() : "");
			}
		}
		if (selectedObjects.Length() == 0) {
			logger::warn("Export slot {} rejected: no exportable objects; previous pattern retained", slot);
			return failure;
		}
		const auto selectedRows = GetReferenceRows(selectedObjects);
		VMArray<TESObjectREFR*> selectedWires = GetSelectedWires(selectedObjects, selectedRows);
		VMArray<BSFixedString> selectedPlugins = GetPlugins(referenceObject, selectedObjects);
		const auto existingPower = CollectExistingPower(workshopRef.get(), selectedObjects, selectedPlugins);

		std::ostringstream patternFileStream;
		patternFileStream.exceptions(std::ios::badbit | std::ios::failbit);
		WriteSelectedGeneralInformation(patternFileStream, patternName, characterName, workshopRef.get(),
			selectedWires.Length() + static_cast<UInt32>(existingPower.wires.size()), selectedObjects.Length(), selectedPlugins);
		WriteSelectedReferenceInformation(patternFileStream, referenceObject, selectedPlugins);
		WriteSelectedPlugins(patternFileStream, selectedPlugins);
		WriteSelectedWires(patternFileStream, selectedWires, selectedRows);
		WriteSelectedObjects(patternFileStream, referenceObject, selectedObjects, selectedPlugins);
		ExistingPower::Write(patternFileStream, existingPower.endpoints, existingPower.wires);
		const std::string serialized = patternFileStream.str();
		const Clipboard::PatternExport::Counts counts{ selectedObjects.Length(), selectedPlugins.Length(),
			selectedWires.Length(), static_cast<UInt32>(existingPower.endpoints.size()), static_cast<UInt32>(existingPower.wires.size()) };
		if (!Clipboard::PatternExport::Validate(serialized, counts)) {
			logger::error("Export slot {} rejected an invalid snapshot; previous pattern retained", slot);
			return failure;
		}

		// Allocate and populate the result before committing. A failed replacement
		// must not be mistaken for success by rereading an older valid destination.
		PatternGeneralEntry result;
		bool populated = result.Set("patternName", patternName);
		populated &= result.Set("characterName", characterName);
		populated &= result.Set<UInt32>("workshopId", workshopRef->formID & 0xFFFFFFu);
		populated &= result.Set<UInt32>("workshopPlugin", static_cast<UInt32>(GetSelectedPluginIndex(workshopRef.get(), selectedPlugins)));
		populated &= result.Set<UInt32>("pluginCount", counts.plugins);
		populated &= result.Set<UInt32>("wireCount", counts.internalWires + counts.externalWires);
		populated &= result.Set<UInt32>("objectCount", counts.objects);
		if (!populated) { return failure; }

		// Retain the Windows text-file line endings used by the earlier writer.
		std::string fileBytes;
		fileBytes.reserve(serialized.size());
		for (const char character : serialized) {
			if (character == '\n') { fileBytes.push_back('\r'); }
			fileBytes.push_back(character);
		}
		const auto written = Clipboard::AtomicFile::Write(GetPatternFilePath(slot), fileBytes);
		if (!written) {
			logger::error("Export slot {} failed at stage {} (Windows error {}); previous pattern retained",
				slot, static_cast<unsigned>(written.stage), written.error);
			return failure;
		}
		return result;
	} catch (const std::exception& error) {
		logger::error("Export slot {} failed before completion: {}", slot, error.what());
		return failure;
	} catch (...) {
		logger::error("Export slot {} failed before completion", slot);
		return failure;
	}
}

// Report the script's own compiled constant, never substitute the DLL's value.
// Restrict storage and output to the maintained script set, once per script/build
// per process. No saved properties or import state are involved.
bool ReportScriptBuild(StaticFunctionTag*, BSFixedString scriptName, UInt32 build)
{
	static constexpr std::array<std::string_view, 16> scripts{
		"ClipboardButtonStand", "ClipboardCopyPylonScript", "ClipboardExtension", "ClipboardManager",
		"ClipboardQuest", "ClipboardSelectionMethod", "ClipboardSelectionMethodAll", "ClipboardSelectionMethodBox",
		"ClipboardSelectionMethodByCell", "ClipboardSelectionMethodByPlugin", "ClipboardSelectionMethodCylinder",
		"ClipboardSelectionMethodGun", "ClipboardSelectionMethodSphere", "ClipboardSelectionWeapon",
		"ClipboardSelectionWeaponHitEffect", "ClipboardToolScript"
	};
	const std::string_view name{ scriptName.c_str() ? scriptName.c_str() : "" };
	const auto found = std::find(scripts.begin(), scripts.end(), name);
	if (found == scripts.end()) { return false; }
	static std::mutex mutex;
	static std::array<std::optional<UInt32>, scripts.size()> reported{};
	const std::lock_guard lock(mutex);
	auto& previous = reported[static_cast<std::size_t>(found - scripts.begin())];
	if (previous && *previous == build) { return false; }
	logger::info("Clipboard build identity: script={} internal={} dll={} match={}",
		name, build, Version::INTERNAL_BUILD, build == Version::INTERNAL_BUILD);
	previous = build;
	return true;
}

bool IsOwnedInputAvailable(StaticFunctionTag*) { return Clipboard::InputService::Available(); }
BSFixedString BeginOwnedInput(StaticFunctionTag*, TESObjectREFR* owner, BSFixedString header,
    BSFixedString initial, std::int32_t inputType, std::int32_t maxChars,
    std::int32_t minimum, std::int32_t maximum)
{
    return BSFixedString(Clipboard::InputService::Begin(owner, header.c_str(), initial.c_str(),
        inputType, maxChars, minimum, maximum));
}
std::int32_t GetOwnedInputState(StaticFunctionTag*, BSFixedString token) { return Clipboard::InputService::State(token.c_str()); }
BSFixedString GetOwnedInputResult(StaticFunctionTag*, BSFixedString token) { return BSFixedString(Clipboard::InputService::Result(token.c_str())); }
bool IsOwnedInputFinished(StaticFunctionTag*, BSFixedString token) { return Clipboard::InputService::Finished(token.c_str()); }
bool AcknowledgeOwnedInput(StaticFunctionTag*, BSFixedString token) { return Clipboard::InputService::Acknowledge(token.c_str()); }
void CancelOwnedInput(StaticFunctionTag*, TESObjectREFR* owner) { Clipboard::InputService::Cancel(owner); }
void AbandonOwnedInput(StaticFunctionTag*, BSFixedString token) { Clipboard::InputService::Abandon(token.c_str()); }

//Trigger a settings load when game data first becomes available.
void MessageCallbackImpl(F4SE::MessagingInterface::Message* msg) {
	if (msg && (msg->type == F4SE::MessagingInterface::kPreLoadGame ||
		msg->type == F4SE::MessagingInterface::kNewGame)) {
		Clipboard::Latent::ResetImportJobs();
		ResetLegacyCleanupSnapshot();
		Clipboard::InputService::Reset();
	}
	if (msg && msg->type == F4SE::MessagingInterface::kGameDataReady) {
		// F4SE sends this message both before loading (null data) and after
		// settings/game data are ready (nonnull data).
		if (msg->data) {
			Clipboard::Localization::RefreshRuntimeLanguage();
		}
		LoadSettings();
		Clipboard::InputUI::Register();
		CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("MessageCallback: Game Data Ready"));
	}
}

void F4SEAPI MessageCallback(F4SE::MessagingInterface::Message* msg)
{
	if (!startupCallbackGate.IsEnabled()) {
		return;
	}
	try {
		MessageCallbackImpl(msg);
	} catch (const std::exception& error) {
		try { logger::error("Clipboard message callback failed: {}", error.what()); } catch (...) {}
	} catch (...) {
		try { logger::error("Clipboard message callback failed with an unknown exception"); } catch (...) {}
	}
}

//Build the full selection box
VMArray<TESObjectREFR*> CreateSelectionBoxLatent(UInt32 stackId, StaticFunctionTag* base, TESObjectREFR* toolObj, TESForm* wallForm, UInt32 xLength, UInt32 yLength, UInt32 zLength) {
	VirtualMachine* vm = GetVirtualMachine();
	VMArray<TESObjectREFR*> newObjs;
	CLIPBOARD_DEBUG_LOG(logger::info(
		"CreateSelectionBox stack {}: tool {:08X}, wall {:08X}, dimensions {}x{}x{}",
		stackId,
		toolObj ? toolObj->formID : 0,
		wallForm ? wallForm->formID : 0,
		xLength,
		yLength,
		zLength));
	if (!vm || !toolObj || !toolObj->parentCell || !wallForm) {
		logger::error("CreateSelectionBox rejected invalid VM, tool, cell, or wall input");
		return newObjs;
	}
	TESWorldSpace* worldspace = toolObj->GetWorldSpace();
	UInt32 invalidHandle = Clipboard::EngineAPI::InvalidRefHandle();

	float sin1 = sin(-toolObj->data.angle.z);
	float cos1 = cos(-toolObj->data.angle.z);

	float Y_OFFSET = TOOL_OFFSET;
	float SEGMENT_SIZE = 500.0;
	float WALL_BASE_SIZE = 256.0;
	float scale = SEGMENT_SIZE / WALL_BASE_SIZE;

	float halfHeight = zLength * 250;
	float halfWidth = xLength * 250;
	float yUpperBound = yLength * 500 + Y_OFFSET;

	for (UInt32 z = 0; z < zLength; z++) {
		float zPos = -halfHeight + z * SEGMENT_SIZE;
		for (UInt32 x = 0; x < xLength; x++) {
			float xPos = -halfWidth + SEGMENT_SIZE / 2.0 + x * SEGMENT_SIZE;

			TESObjectREFR* newObj = PlaceAtMe_Native(vm, stackId, &toolObj, wallForm, 1, true, true, false);

			if (newObj) {
				float relativeX = xPos * cos1 - Y_OFFSET * sin1;
				float relativeY = Y_OFFSET * cos1 + xPos * sin1;
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Created Selection Wall 1 z: " + std::to_string(z) + " x: " + std::to_string(x)));
				newObj->data.location.x = toolObj->data.location.x + relativeX;
				newObj->data.location.y = toolObj->data.location.y + relativeY;
				newObj->data.location.z = toolObj->data.location.z + zPos;
				newObj->data.angle.z = toolObj->data.angle.z + PI / 2.0;
				//Apply scale
				if (scale != 1.0) {
					CALL_MEMBER_FN(newObj, SetScale)(scale);
				}
				//Set Position/Rotation
				MoveRefrToPosition(newObj, &invalidHandle, toolObj->parentCell, worldspace, &newObj->data.location, &newObj->data.angle);

				Enable_Native(newObj, false);
				newObjs.Push(&newObj);
			}

			newObj = PlaceAtMe_Native(vm, stackId, &toolObj, wallForm, 1, true, true, false);

			if (newObj) {

				float relativeX = xPos * cos1 - yUpperBound * sin1;
				float relativeY = yUpperBound * cos1 + xPos * sin1;
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Created Selection Wall 2 z: " + std::to_string(z) + " x: " + std::to_string(x)));
				newObj->data.location.x = toolObj->data.location.x + relativeX;
				newObj->data.location.y = toolObj->data.location.y + relativeY;
				newObj->data.location.z = toolObj->data.location.z + zPos;
				newObj->data.angle.z = toolObj->data.angle.z + PI / 2.0;
				//Apply scale
				if (scale != 1.0) {
					CALL_MEMBER_FN(newObj, SetScale)(scale);
				}
				//Set Position/Rotation
				MoveRefrToPosition(newObj, &invalidHandle, toolObj->parentCell, worldspace, &newObj->data.location, &newObj->data.angle);
				//Call twice to fix jitter effect
				MoveRefrToPosition(newObj, &invalidHandle, toolObj->parentCell, worldspace, &newObj->data.location, &newObj->data.angle);

				Enable_Native(newObj, false);
				newObjs.Push(&newObj);
			}
		}



		for (UInt32 y = 0; y < yLength; y++) {
			float yPos = SEGMENT_SIZE / 2.0 + y * SEGMENT_SIZE + Y_OFFSET;

			TESObjectREFR* newObj = PlaceAtMe_Native(vm, stackId, &toolObj, wallForm, 1, true, true, false);

			if (newObj) {
				float relativeX = -halfWidth * cos1 - yPos * sin1;
				float relativeY = yPos * cos1 - halfWidth * sin1;
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Created Selection Wall 1 z: " + std::to_string(z) + " y: " + std::to_string(y)));
				newObj->data.location.x = toolObj->data.location.x + relativeX;
				newObj->data.location.y = toolObj->data.location.y + relativeY;
				newObj->data.location.z = toolObj->data.location.z + zPos;
				newObj->data.angle.z = toolObj->data.angle.z;
				//Apply scale
				if (scale != 1.0) {
					CALL_MEMBER_FN(newObj, SetScale)(scale);
				}
				//Set Position/Rotation
				MoveRefrToPosition(newObj, &invalidHandle, toolObj->parentCell, worldspace, &newObj->data.location, &newObj->data.angle);
				//Call twice to fix jitter effect
				MoveRefrToPosition(newObj, &invalidHandle, toolObj->parentCell, worldspace, &newObj->data.location, &newObj->data.angle);

				Enable_Native(newObj, false);
				newObjs.Push(&newObj);
			}

			newObj = PlaceAtMe_Native(vm, stackId, &toolObj, wallForm, 1, true, true, false);

			if (newObj) {

				float relativeX = halfWidth * cos1 - yPos * sin1;
				float relativeY = yPos * cos1 + halfWidth * sin1;
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Created Selection Wall 2 z: " + std::to_string(z) + " x: " + std::to_string(y)));
				newObj->data.location.x = toolObj->data.location.x + relativeX;
				newObj->data.location.y = toolObj->data.location.y + relativeY;
				newObj->data.location.z = toolObj->data.location.z + zPos;
				newObj->data.angle.z = toolObj->data.angle.z;
				//Apply scale
				if (scale != 1.0) {
					CALL_MEMBER_FN(newObj, SetScale)(scale);
				}
				//Set Position/Rotation
				MoveRefrToPosition(newObj, &invalidHandle, toolObj->parentCell, worldspace, &newObj->data.location, &newObj->data.angle);
				//Call twice to fix jitter effect
				MoveRefrToPosition(newObj, &invalidHandle, toolObj->parentCell, worldspace, &newObj->data.location, &newObj->data.angle);

				Enable_Native(newObj, false);
				newObjs.Push(&newObj);
			}
		}
	}
	CLIPBOARD_DEBUG_LOG(logger::info("CreateSelectionBox stack {} created {} cage references", stackId, newObjs.Length()));
	return newObjs;
}

//Send the given workshop event to all selected objects. 
//Send the given workshop event to the workshop for each selected object.
void SendWorkshopEventToSelectedObjectsLatent(UInt32 stackId, StaticFunctionTag* base, TESObjectREFR* refObj, BSFixedString functionName) {
	TESObjectREFR* workshopRef = GetLinkedRef_Native(refObj, workshopItemKeyword);
	VirtualMachine* vm = GetVirtualMachine();
	if (!vm || !workshopRef || functionName.empty()) {
		return;
	}

	VMArray<TESObjectREFR*> objs = GetSelectedObjectReferences(base, refObj);
	TESObjectREFR* obj;
	for (UInt32 i = 0; i < objs.Length(); i++) {
		objs.Get(&obj, i);
		if (!obj) {
			continue;
		}

		if (!Clipboard::EngineAPI::CallFunctionNoWait(vm, workshopRef, functionName, obj)) {
			logger::error("Failed to call {} on workshop reference", functionName.c_str());
		}
		if (!Clipboard::EngineAPI::CallFunctionNoWait(vm, obj, functionName, workshopRef)) {
			logger::error("Failed to call {} on selected object", functionName.c_str());
		}
	}
}

//Call Enable on all objects in the given array.
void EnableObjectsLatent(UInt32 stackId, StaticFunctionTag* base, VMArray<TESObjectREFR*> objs) {

	TESObjectREFR* obj;
	for (UInt32 j = 0; j < objs.Length(); j++) {
		objs.Get(&obj, j);
		if (obj) {
			Enable_Native(obj, false);
		}
	}
}

//Call Disable on all objects in the given array.
void DisableObjectsLatent(UInt32 stackId, StaticFunctionTag* base, VMArray<TESObjectREFR*> objs) {

	TESObjectREFR* obj;
	for (UInt32 j = 0; j < objs.Length(); j++) {
		objs.Get(&obj, j);
		if (obj) {
			Disable_Native(obj, false);
		}
	}
}
bool ScrapLatent(UInt32 stackId, TESObjectREFR* refr, TESObjectREFR* akWorkshopRef)
{
	if (!refr || refr->IsDeleted()) {
		return false;
	}
	auto* player = PlayerCharacter::GetSingleton();
	if (!player) {
		return false;
	}
	// Workshop::ScrapReference owns the WorkshopItem unlink together with the
	// workshop and power-grid teardown.  The official F4SE path enters it with
	// the ownership link intact, so prefer that owner over proximity and do not
	// erase the link before the engine call.
	auto isWorkshop = [](TESObjectREFR* a_reference) {
		return a_reference && !a_reference->IsDeleted() && a_reference->extraList &&
			a_reference->extraList->GetByType<Workshop::ExtraData>();
	};
	TESObjectREFR* linkedWorkshop = workshopItemKeyword ?
		GetLinkedRef_Native(refr, workshopItemKeyword) : nullptr;
	if (linkedWorkshop && !isWorkshop(linkedWorkshop)) {
		logger::warn(
			"Scrap stack {} reference {:08X} has invalid WorkshopItem target {:08X}; ignoring it",
			stackId,
			refr->formID,
			linkedWorkshop->formID);
		linkedWorkshop = nullptr;
	}
	if (linkedWorkshop) {
		if (akWorkshopRef && akWorkshopRef != linkedWorkshop) {
			logger::warn(
				"Scrap stack {} reference {:08X} nearest workshop {:08X} differs from WorkshopItem owner {:08X}; using owner",
				stackId,
				refr->formID,
				akWorkshopRef->formID,
				linkedWorkshop->formID);
		}
		akWorkshopRef = linkedWorkshop;
	}
	if (akWorkshopRef && !isWorkshop(akWorkshopRef)) {
		akWorkshopRef = nullptr;
	}
	if (!akWorkshopRef) {
		akWorkshopRef = Workshop::FindNearestValidWorkshop(*refr);
	}
	if (!isWorkshop(akWorkshopRef)) {
		logger::warn("Scrap stack {} reference {:08X} has no workshop", stackId, refr->formID);
		return false;
	}
	if (!player->parentCell) {
		return false;
	}
	Workshop::ContextData contextData(player);
	ScopedCurrentWorkshop currentWorkshop{ akWorkshopRef };
	Clipboard::EngineAPI::ScrapReference(&contextData, refr);
	return true;
}

using ScrapTarget = Clipboard::Scrapping::Target<NiPointer<TESObjectREFR>>;
using ScrapBatch = Clipboard::Scrapping::Batch<NiPointer<TESObjectREFR>>;

ScrapBatch SnapshotScrapTargets(
	const VMArray<TESObjectREFR*>& a_objects)
{
	ScrapBatch batch;
	batch.Reserve(a_objects.Length());
	std::set<UInt32> targetFormIDs;

	TESObjectREFR* object = nullptr;
	for (UInt32 i = 0; i < a_objects.Length(); i++) {
		object = nullptr;
		if (!a_objects.Get(&object, i) || !object || object->IsDeleted() || !targetFormIDs.insert(object->formID).second) {
			continue;
		}

		TESObjectREFR* workshop = workshopItemKeyword ?
			GetLinkedRef_Native(object, workshopItemKeyword) : nullptr;
		if (!workshop) {
			workshop = Workshop::FindNearestValidWorkshop(*object);
		}
		batch.Add(object, workshop);
	}
	auto& targets = batch.Targets();
	std::sort(
		targets.begin(),
		targets.end(),
		[](const ScrapTarget& a_left, const ScrapTarget& a_right) {
			return a_left.reference->formID < a_right.reference->formID;
		});

	return batch;
}

ScrapBatch SnapshotPacedScrapTargets(const VMArray<TESObjectREFR*>& objects)
{
	return SnapshotScrapTargets(objects);
}

void ScrapTargetsWithEngineManagedWires(
	UInt32 a_stackID,
	const VMArray<TESObjectREFR*>& a_objects,
	bool a_clearSelectionLinks)
{
	// Snapshot and sort requested references before mutation. The engine owns
	// attached-wire teardown; never dispatch those wires a second time.
	const ScrapBatch batch = SnapshotScrapTargets(a_objects);
	const auto& targets = batch.Targets();

	(void)Clipboard::Scrapping::DispatchLiveTargets(
		targets,
		[](TESObjectREFR* reference) { return reference->IsDeleted(); },
		[&](TESObjectREFR* reference, TESObjectREFR* workshop) {
			if (a_clearSelectionLinks) {
				SetLinkedRef_Native(reference, nullptr, clipboardSelectedKeyword);
			}
			return ScrapLatent(a_stackID, reference, workshop);
		});

}

//Scrap all selected objects.
//   Workshop::ScrapReference removes each selected object's attached wires.
//   Does not return components.
void ScrapSelectionLatent(UInt32 stackId, StaticFunctionTag* base, TESObjectREFR* refObj) {

	VMArray<TESObjectREFR*> selectedObjs = GetSelectedObjectReferences(base, refObj);

	SetLinkedRef_Native(refObj, nullptr, clipboardSelectedKeyword);
	ScrapTargetsWithEngineManagedWires(stackId, selectedObjs, true);
}
//Scrap all objects in the given array.
//   Workshop::ScrapReference removes each object's attached wires.
//   Does not return components.
void ScrapObjectsLatent(UInt32 stackId, StaticFunctionTag* base, VMArray<TESObjectREFR*> objs) {

	ScrapTargetsWithEngineManagedWires(stackId, objs, false);
}

//Scale all selected objects by the given amount.
//   If maintainShape, scale the relative position to the reference object as well.
//   Restore (scale <= 0) sets every object to 1.0; mixed groups keep positions.
//   Minimum resultent scale is 0.01
//   Maximum resultent scale is 10.0
Clipboard::ScaleConstraints::Selection ReadScaleConstraints(const VMArray<TESObjectREFR*>& objects) {

	Clipboard::ScaleConstraints::Selection selection;
	for (auto* obj : objects) {
		if (!obj || obj->IsDeleted()) { selection.valid = false; }
		else { selection.Add(obj->refScale); }
	}
	return selection;
}

std::string DescribeScaleRestriction(const Clipboard::ScaleConstraints::Selection& selection,
	std::uint64_t requestedMicros, bool increase) {

	using namespace Clipboard::ScaleConstraints;
	if (!selection) {
		return Clipboard::Localization::GetRuntimeText("$Clipboard_ScaleSelectionInvalid", {});
	}
	const auto choices = selection.Choices(requestedMicros, increase);
	if (!choices.maximum) {
		return Clipboard::Localization::GetRuntimeText("$Clipboard_ScaleNoExactChange", {});
	}
	const auto step = PercentText(choices.step) + "%";
	const auto maximum = PercentText(choices.maximum) + "%";
	auto nearby = choices.lower ? PercentText(choices.lower) + "%" : std::string{};
	if (choices.upper) {
		if (!nearby.empty()) { nearby += ", "; }
		nearby += PercentText(choices.upper) + "%";
	}
	return Clipboard::Localization::GetRuntimeText("$Clipboard_ScaleRestricted", {step, maximum, nearby});
}

BSFixedString GetSelectionScaleInputError(StaticFunctionTag* base, TESObjectREFR* refObj,
	BSFixedString value, bool increase) {

	const auto selection = refObj && !refObj->IsDeleted() ?
		ReadScaleConstraints(GetSelectedObjectReferences(base, refObj)) : Clipboard::ScaleConstraints::Selection{};
	if (selection.CheckInput(value.c_str(), increase)) { return BSFixedString(""); }
	Clipboard::Input::Request request;
	request.inputType = 1; request.minimum = 0; request.maximum = increase ? 1000 : 99; request.maxChars = 64;
	const auto parsed = Clipboard::Input::Validate(request, value.c_str());
	return BSFixedString(DescribeScaleRestriction(selection, parsed.decimalMicros, increase));
}

BSFixedString GetSelectionScaleError(StaticFunctionTag* base, TESObjectREFR* refObj, float factor) {

	const auto selection = refObj && !refObj->IsDeleted() ?
		ReadScaleConstraints(GetSelectedObjectReferences(base, refObj)) : Clipboard::ScaleConstraints::Selection{};
	if (selection && std::isfinite(factor) && (factor <= 0 || selection.CheckFactor(factor))) {
		return BSFixedString("");
	}
	const auto micros = std::isfinite(factor) ? static_cast<std::uint64_t>(
		(std::min)(1000.0, std::abs(static_cast<double>(factor) - 1.0) * 100.0) * 1000000.0) : 0;
	return BSFixedString(DescribeScaleRestriction(selection, micros, factor >= 1));
}

bool TryScaleSelectionLatent(UInt32 stackId, StaticFunctionTag* base, TESObjectREFR* refObj, float scale, bool maintainShape) {

	if (!refObj || refObj->IsDeleted() || !std::isfinite(scale)) {
		return false;
	}
	// Use one selection snapshot for geometry and mutation. Scaling has no use
	// for the full details query's wire/plugin walks or VM struct marshaling.
	const auto selectedObjs = GetSelectedObjectReferences(base, refObj);
	const auto constraints = ReadScaleConstraints(selectedObjs);
	const auto ratio = constraints.CheckFactor(scale);
	if (!constraints || (scale > 0 && !ratio)) {
		CLIPBOARD_DEBUG_LOG(logger::info("ScaleSelection rejected before mutation: factor {:.9g}", scale));
		return false;
	}
	const auto geometry = MeasureSelectedGeometry(selectedObjs);
	auto plan = Clipboard::SelectionGeometry::PlanScale(geometry, scale, maintainShape);
	if (!plan.restore) { plan.factor = ratio.Factor(); }

	UInt32 invalidHandle = Clipboard::EngineAPI::InvalidRefHandle();
	TESWorldSpace* worldspace = refObj->GetWorldSpace();

	float centerX = refObj->data.location.x;
	float centerY = refObj->data.location.y;
	// SelectionDetails has no minimumZ member.  The v221 VMStruct lookup left
	// this initialized reference-object pivot unchanged when that probe failed.
	float minimumZ = refObj->data.location.z;
	if (plan.movePositions) {
		const auto center = geometry.Center();
		centerX = center.x;
		centerY = center.y;
	}

	const float finalScale = plan.factor;
	CLIPBOARD_DEBUG_LOG(logger::info(
		"ScaleSelection stack {}: {} objects, requested factor {:.9g}, applied factor {:.9g}, maintainShape={}, restore={}, movePositions={}",
		stackId,
		selectedObjs.Length(),
		scale,
		finalScale,
		maintainShape, plan.restore, plan.movePositions));
	TESObjectREFR* obj;
	for (UInt32 i = 0; i < selectedObjs.Length(); i++) {
		selectedObjs.Get(&obj, i);
		if (!obj) {
			continue;
		}

		const float oldScale = GetScale(obj);
		const float newScale = plan.restore ? 1.0F : static_cast<float>(ratio.Target(obj->refScale)) / 100.0F;
		CALL_MEMBER_FN(obj, SetScale)(newScale);
		CLIPBOARD_DEBUG_LOG(logger::info(
			"ScaleSelection reference {:08X}: before {:.9g}, requested {:.9g}, stored {:.9g}",
			obj->formID,
			oldScale,
			newScale, GetScale(obj)));
		if (plan.movePositions) {
			obj->data.location.x = (obj->data.location.x - centerX) * finalScale + centerX;
			obj->data.location.y = (obj->data.location.y - centerY) * finalScale + centerY;
			obj->data.location.z = (obj->data.location.z - minimumZ) * finalScale + minimumZ;
			MoveRefrToPosition(obj, &invalidHandle, obj->parentCell, worldspace, &obj->data.location, &obj->data.angle);
		}
	}
	return true;
}

// Preserve the old void binding and serialized functor for external/saved calls.
void ScaleSelectionLatent(UInt32 stackId, StaticFunctionTag* base, TESObjectREFR* refObj, float scale, bool maintainShape) {
	(void)TryScaleSelectionLatent(stackId, base, refObj, scale, maintainShape);
}

// Power callbacks construct bounded workers; all graph work runs on the engine task queue.
#include "PowerWorkJobs.inl"

namespace
{
	using DuplicateTransform = Clipboard::ImportDuplicates::Transform;
	struct DuplicateReference
	{
		NiPointer<TESObjectREFR> reference;
		bool createdHere{ false };
	};
	using DuplicateIndex = Clipboard::ImportDuplicates::Index<DuplicateReference>;

	DuplicateTransform ReadDuplicateTransform(TESObjectREFR* reference)
	{
		const auto& pos = reference->data.location;
		const auto& rot = reference->data.angle;
		return { { pos.x, pos.y, pos.z }, { rot.x, rot.y, rot.z }, GetScale(reference) };
	}

	bool GetPatternWorldTransform(PatternObjectEntry& entry, TESObjectREFR* tool, NiPoint3& pos, NiPoint3& rot)
	{
		pos = {}; rot = {};
		entry.Get<float>("positionX", &pos.x);
		entry.Get<float>("positionY", &pos.y);
		entry.Get<float>("positionZ", &pos.z);
		entry.Get<float>("angleX", &rot.x);
		entry.Get<float>("angleY", &rot.y);
		entry.Get<float>("angleZ", &rot.z);
		return Clipboard::ImportTransform::Compose(tool->data.location, tool->data.angle, pos, rot, pos, rot);
	}

	DuplicateIndex SnapshotImportDuplicates(TESObjectREFR* tool, VMArray<PatternObjectEntry>& entries)
	{
		// Restrict the retained snapshot to the pattern's composed bounds. Include
		// vanilla/persistent references without applying selection/export filters.
		NiPoint3 minimum{}, maximum{};
		bool haveBounds = false;
		for (auto& entry : entries) {
			NiPoint3 pos{}, rot{};
			if (!GetPatternWorldTransform(entry, tool, pos, rot)) { continue; }
			if (!haveBounds) { minimum = maximum = pos; haveBounds = true; }
			minimum.x = std::min(minimum.x, pos.x); maximum.x = std::max(maximum.x, pos.x);
			minimum.y = std::min(minimum.y, pos.y); maximum.y = std::max(maximum.y, pos.y);
			minimum.z = std::min(minimum.z, pos.z); maximum.z = std::max(maximum.z, pos.z);
		}
		DuplicateIndex index;
		if (!haveBounds) { return index; }
		std::vector<NiPointer<TESObjectREFR>> references;
		{
			const auto [forms, lock] = TESForm::GetAllForms();
			BSAutoReadLock readLock{ lock };
			if (forms) {
				for (const auto& [id, form] : *forms) {
					if (!form || !form->Is(ENUM_FORM_ID::kREFR, ENUM_FORM_ID::kACHR)) { continue; }
					auto* reference = static_cast<TESObjectREFR*>(form);
					const auto& pos = reference->data.location;
					constexpr auto e = Clipboard::ImportDuplicates::kPositionTolerance;
					if (reference != tool && SharesWorkshopSpace(reference->parentCell, tool->parentCell) &&
						pos.x >= minimum.x - e && pos.x <= maximum.x + e &&
						pos.y >= minimum.y - e && pos.y <= maximum.y + e &&
						pos.z >= minimum.z - e && pos.z <= maximum.z + e) {
						references.emplace_back(reference);
					}
				}
			}
		}
		std::sort(references.begin(), references.end(), [](const auto& left, const auto& right) {
			return left->formID < right->formID;
		});
		for (auto& reference : references) {
			auto* form = reference->data.objectReference;
			if (form && !(reference->GetFormFlags() & ((1u << 5) | (1u << 11)))) {
				index.Add(form->formID, ReadDuplicateTransform(reference.get()), { reference, false });
			}
		}
		return index;
	}

	struct ImportPlacementRows
	{
		VMArray<TESObjectREFR*> newRows;
		VMArray<TESObjectREFR*> wireRows;
		std::int32_t reusedRows{ 0 };
		DuplicateIndex duplicateHolds; // Retain through packing into VM-owned row arrays.
	};
}

//Creates objects as defined by the clipboard pattern.
static ImportPlacementRows PlacePatternRows(UInt32 stackId, StaticFunctionTag* base, TESObjectREFR* refObj, UInt32 slot, bool reuseDuplicates) {
	ScopedImportLog traceScope;
	Clipboard::ImportProgress::Notice importNotice;
	importNotice.Start(UseWorkshopThrottling());

	VirtualMachine* vm = GetVirtualMachine();
	ImportPlacementRows result;
	auto& newObjs = result.newRows;
	if (!vm || !refObj || !refObj->parentCell) {
		return result;
	}
	TESWorldSpace* worldspace = refObj->GetWorldSpace();
	TESObjectREFR* workshopRef = GetLinkedRef_Native(refObj, workshopItemKeyword);
	UInt32 invalidHandle = Clipboard::EngineAPI::InvalidRefHandle();
	VMArray<BSFixedString> plugins = GetPatternPlugins(nullptr, slot);
	VMArray<PatternObjectEntry> objectEntries = GetPatternObjects(base, slot);
	for (UInt32 i = 0; i < objectEntries.Length(); ++i) {
		result.newRows.push_back(static_cast<TESObjectREFR*>(nullptr));
		result.wireRows.push_back(static_cast<TESObjectREFR*>(nullptr));
	}
	result.duplicateHolds = reuseDuplicates ? SnapshotImportDuplicates(refObj, objectEntries) : DuplicateIndex{};
	auto& duplicates = result.duplicateHolds;
	PatternObjectEntry objectEntry;
	TESObjectREFR* lastObj = refObj;
	for (UInt32 i = 0; i < objectEntries.Length(); i++) {
		importNotice.Update();
		CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Start Pasting " + std::to_string(i)));
		objectEntries.Get(&objectEntry, i);
		UInt32 declaredPluginIndex = 0;
		UInt32 declaredFormId = 0;
		BSFixedString declaredPlugin;
		if (objectEntry.Get<UInt32>("pluginIndex", &declaredPluginIndex) &&
			objectEntry.Get<UInt32>("formId", &declaredFormId) &&
			declaredPluginIndex < plugins.Length() &&
			plugins.Get(&declaredPlugin, declaredPluginIndex)) {
			const std::string declaredPluginName = declaredPlugin.c_str();
			if (istarts_with(declaredPluginName, "Clipboard.") ||
				IsPluginBlackListed(declaredPluginName) ||
				IsFormIdBlackListed(declaredPluginName, declaredFormId)) {
				logger::warn(
					"Import skipped blocked pattern object row {} before FormID resolution "
					"(source {}, local form {})",
					i,
					declaredPluginName,
					declaredFormId);
				continue;
			}
		}
		float scale = 1.0F;
		objectEntry.Get<float>("scale", &scale);

		NiPoint3 pos{}, rot{};
		if (!GetPatternWorldTransform(objectEntry, refObj, pos, rot)) {
			logger::warn("Import skipped pattern object row {}: non-finite source or composed transform", i);
			continue;
		}

		TESForm* form = nullptr;
		try {
			form = LookupFormByID(ToFormID(objectEntry, plugins));
			if (!form) {
				AppendToLog("Missing form, use a bottlecap");
				form = LookupFormByID(BOTTLECAP_FORM_ID);
			}
		}
		catch (...) {
			AppendToLog("Error loading form, use a bottlecap.");
			form = LookupFormByID(BOTTLECAP_FORM_ID);
		}

		if (form && form->formID != BOTTLECAP_FORM_ID &&
			(IsWorkshop(form) ||
			 istarts_with(GetPluginName(form).c_str(), "Clipboard.") ||
			 IsBaseFormStructurallyBlockedForImport(form) ||
			 IsBaseFormBlockedForTransfer(form, true))) {
			logger::warn(
				"Import skipped unsafe pattern object row {} (base {:08X}, source {}, type {})",
				i,
				form->formID,
				GetPluginName(form).c_str(),
				static_cast<UInt32>(form->GetFormType()));
			continue;
		}

		// Unresolved forms still get one bottlecap per physical row. Reused rows
		// never acquire selection/workshop links or undergo import preparation.
		const DuplicateTransform target{ { pos.x, pos.y, pos.z }, { rot.x, rot.y, rot.z },
			scale >= 0.01F && scale <= 10.0F ? scale : 1.0F };
		if (reuseDuplicates && form && form->formID != BOTTLECAP_FORM_ID) {
			const auto* match = duplicates.Find(form->formID, target, [&](const DuplicateReference& candidate) {
				auto* reference = candidate.reference.get();
				return reference && !reference->IsDeleted() &&
					(candidate.createdHere || !(reference->GetFormFlags() & (1u << 11))) &&
					reference->data.objectReference == form &&
					SharesWorkshopSpace(reference->parentCell, refObj->parentCell) &&
					Clipboard::ImportDuplicates::Matches(ReadDuplicateTransform(reference), target);
			});
			if (match) {
				result.wireRows[i] = match->reference.get();
				++result.reusedRows;
				continue;
			}
		}

		try {
			TESObjectREFR* newObj = PlaceAtMe_Native(vm, stackId, &refObj, form, 1, true, true, false);

			if (newObj) {
				CLIPBOARD_IMPORT_DETAIL_LOG(importLogDepth != 0, enableVerboseImportLogging, AppendToLog("Successfully created new object"));
				SetLinkedRef_Native(newObj, workshopRef, workshopItemKeyword);

				//Apply scale
				if (scale != 1.0 && scale >= 0.01 && scale <= 10.0) {
					CALL_MEMBER_FN(newObj, SetScale)(scale);
				}

				//Set Position/Rotation
				MoveRefrToPosition(newObj, &invalidHandle, refObj->parentCell, worldspace, &pos, &rot);

				//Add to selection chain
				SetLinkedRef_Native(lastObj, newObj, clipboardSelectedKeyword);

				lastObj = newObj;
				newObjs[i] = newObj;
				result.wireRows[i] = newObj;
				if (reuseDuplicates && form && form->formID != BOTTLECAP_FORM_ID) {
					duplicates.Add(form->formID, ReadDuplicateTransform(newObj), { NiPointer<TESObjectREFR>{ newObj }, true });
				}

			}
			else {
				AppendToLog("Failed to create new object");
			}
		}
		catch (...) {
			AppendToLog("Error creating new object.");
		}
	}
	SetLinkedRef_Native(lastObj, nullptr, clipboardSelectedKeyword);

	return result;
}

// Preserve the public legacy placement contract and its serialized factory.
VMArray<TESObjectREFR*> PastePatternObjectsLatent(UInt32 stackId, StaticFunctionTag* base, TESObjectREFR* refObj, UInt32 slot)
{
	return PlacePatternRows(stackId, base, refObj, slot, false).newRows;
}

VMArray<ImportPlacementRow> PastePatternObjectsWithReuseLatent(UInt32 stackId, StaticFunctionTag* base, TESObjectREFR* refObj, UInt32 slot)
{
	auto rows = PlacePatternRows(stackId, base, refObj, slot, true);
	VMArray<ImportPlacementRow> result;
	for (UInt32 i = 0; i < rows.newRows.Length(); ++i) {
		ImportPlacementRow entry;
		if (!entry.Set("newObject", rows.newRows[i]) || !entry.Set("wireObject", rows.wireRows[i])) {
			throw std::runtime_error("Could not pack an import placement row");
		}
		result.push_back(std::move(entry));
	}
	CLIPBOARD_DEBUG_LOG(logger::info("Import placement stack {}: {} physical rows, {} reused rows for wire endpoints", stackId, rows.newRows.Length(), rows.reusedRows));
	return result;
}

// Intentional None slots and unavailable handles are normal during imports.
// Avoid CommonLib's generic object unpacker, which asserts on those values.
static bool ReadImportPlacementReference(const ImportPlacementRow& entry, const char* name, TESObjectREFR*& reference, bool& present)
{
	reference = nullptr;
	present = false;
	const auto proxy = RE::BSScript::detail::wrapper_accessor::get_proxy(entry);
	if (!proxy || !proxy->type) { return false; }
	const auto found = proxy->type->varNameIndexMap.find(name);
	if (found == proxy->type->varNameIndexMap.end()) { return false; }
	const auto& value = proxy->variables[found->second];
	if (value.is<std::nullptr_t>()) { return true; }
	if (!value.is<RE::BSScript::Object>()) { return false; }
	const auto object = RE::BSScript::get<RE::BSScript::Object>(value);
	if (!object) { return true; }
	present = true;
	auto* vm = GetVirtualMachine();
	if (!vm) { return false; }
	const auto& handles = vm->GetObjectHandlePolicy();
	const auto handle = object->GetHandle();
	if (handles.IsHandleLoaded(handle)) {
		reference = static_cast<TESObjectREFR*>(handles.GetObjectForHandle(RE::BSScript::GetVMTypeID<TESObjectREFR>(), handle));
	}
	return true;
}

VMArray<TESObjectREFR*> GetImportPlacementRows(StaticFunctionTag*, VMArray<ImportPlacementRow> placement, bool forWires)
{
	VMArray<TESObjectREFR*> result;
	for (const auto& entry : placement) {
		TESObjectREFR* reference = nullptr;
		bool present{};
		if (!ReadImportPlacementReference(entry, forWires ? "wireObject" : "newObject", reference, present)) { return {}; }
		result.push_back(reference);
	}
	return result;
}

std::int32_t GetImportPlacementReusedCount(StaticFunctionTag*, VMArray<ImportPlacementRow> placement)
{
	std::int32_t reused{};
	for (const auto& entry : placement) {
		TESObjectREFR* reference = nullptr;
		bool created{}, endpoint{};
		if (!ReadImportPlacementReference(entry, "newObject", reference, created) ||
			!ReadImportPlacementReference(entry, "wireObject", reference, endpoint)) { return -1; }
		// Presence records ownership even when a once-valid handle becomes unavailable.
		reused += !created && endpoint;
	}
	return reused;
}

std::int32_t GetImportedAnimationKind(StaticFunctionTag*, RE::TESObjectREFR* reference)
{
	return Clipboard::ImportAnimation::GetKind(reference);
}

template <auto Function>
struct LegacyStaticAdapter;

template <class Result, class... Args, Result (*Function)(StaticFunctionTag*, Args...)>
struct LegacyStaticAdapter<Function>
{
	static Result Invoke(StaticFunctionTag, Args... args)
	{
		if constexpr (std::is_void_v<Result>) {
			Function(nullptr, std::forward<Args>(args)...);
		} else {
			return Function(nullptr, std::forward<Args>(args)...);
		}
	}
};

#include "LegacyCleanup.inl"

template <class Native>
class GuardedNativeFunction final : public Native
{
public:
	using Native::Native;

	bool MarshallAndDispatch(BSScript::Variable& self, BSScript::Internal::VirtualMachine& vm,
		std::uint32_t stackID, BSScript::Variable& result, const BSScript::StackFrame& frame) const override
	{
		return Clipboard::NativeCallBoundary::Invoke(
			[&] { return Native::MarshallAndDispatch(self, vm, stackID, result, frame); },
			[&](const char* reason) {
				result = nullptr;
				logger::error("ClipboardExtension.{} failed on Papyrus stack {}: {}",
					this->GetName().c_str(), stackID, reason);
			});
	}
};

template <auto Function>
bool BindLegacyStatic(VirtualMachine& vm, std::string_view name)
{
	using Native = decltype(BSScript::NativeFunction(
		kPapyrusClassName, name, &LegacyStaticAdapter<Function>::Invoke, false));
	auto binding = std::unique_ptr<BSScript::IFunction>{ new GuardedNativeFunction<Native>(
		kPapyrusClassName, name, &LegacyStaticAdapter<Function>::Invoke, false) };
	if (!Clipboard::PapyrusBinding::Bind(vm, std::move(binding))) {
		logger::critical("Failed to bind ClipboardExtension.{}; check the matching ClipboardExtension.pex and Papyrus log", name);
		return false;
	}
	CLIPBOARD_DEBUG_LOG(logger::info("Bound ClipboardExtension.{}", name));
	return true;
}

template <class Structure>
bool ResolvePapyrusStructure(VirtualMachine& vm)
{
	const BSFixedString qualifiedName{ Structure::name };
	RE::BSTSmartPointer<RE::BSScript::StructTypeInfo> typeInfo;
	const bool resolved =
		vm.GetScriptStructType(qualifiedName, typeInfo) && typeInfo;
	if (resolved) {
		CLIPBOARD_DEBUG_LOG(logger::info(
			"Resolved Papyrus structure descriptor {} ({} characters)",
			Structure::name,
			Structure::name.size()));
	} else {
		logger::critical(
			"Failed to resolve Papyrus structure descriptor {} ({} characters)",
			Structure::name,
			Structure::name.size());
	}
	return resolved;
}

bool RegisterFuncsImpl(VirtualMachine* vm)
{
	if (!vm) {
		logger::critical("Clipboard Papyrus registration received a null VM");
		return false;
	}

	bool structuresResolved = true;
	structuresResolved &= ResolvePapyrusStructure<SelectionDetails>(*vm);
	structuresResolved &= ResolvePapyrusStructure<PatternObjectEntry>(*vm);
	structuresResolved &= ResolvePapyrusStructure<PatternWireEntry>(*vm);
	structuresResolved &= ResolvePapyrusStructure<PatternGeneralEntry>(*vm);
	structuresResolved &= ResolvePapyrusStructure<PatternReferenceEntry>(*vm);
	structuresResolved &= ResolvePapyrusStructure<ComponentEntry>(*vm);
	structuresResolved &= ResolvePapyrusStructure<ImportPlacementRow>(*vm);
	if (!structuresResolved) {
		logger::critical("One or more required Clipboard Papyrus structures could not be resolved");
		return false;
	}

	bool bound = true;
	bound &= BindLegacyStatic<ReportScriptBuild>(*vm, "ReportScriptBuild");
	bound &= BindLegacyStatic<GetImportPlacementRows>(*vm, "GetImportPlacementRows");
	bound &= BindLegacyStatic<Clipboard::Latent::GetSuccessfulImportRows>(*vm, "GetSuccessfulImportRows");
	bound &= BindLegacyStatic<GetImportPlacementReusedCount>(*vm, "GetImportPlacementReusedCount");
	bound &= BindLegacyStatic<UpdateSelectedWires_>(*vm, "UpdateSelectedWires");
	bound &= BindLegacyStatic<GetSelectionDetails>(*vm, "GetSelectionDetails");
	bound &= BindLegacyStatic<GetSelectionCenter>(*vm, "GetSelectionCenter");
	bound &= BindLegacyStatic<RotateSelectionZ>(*vm, "RotateSelectionZ");
	bound &= BindLegacyStatic<MoveSelection>(*vm, "MoveSelection");
	bound &= BindLegacyStatic<ClearSelection>(*vm, "ClearSelection");
	bound &= BindLegacyStatic<DeselectAll>(*vm, "DeselectAll");
	bound &= BindLegacyStatic<Deselect>(*vm, "Deselect");
	bound &= BindLegacyStatic<SelectAll>(*vm, "SelectAll");
	bound &= BindLegacyStatic<Select>(*vm, "Select");
	bound &= BindLegacyStatic<WritePatternFile>(*vm, "WritePatternFile");
	bound &= BindLegacyStatic<GetSelectedPlugins_>(*vm, "GetSelectedPlugins");
	bound &= BindLegacyStatic<GetPlugins_>(*vm, "GetPlugins");
	bound &= BindLegacyStatic<GetSelectedObjectReferences>(*vm, "GetSelectedObjectReferences");
	bound &= BindLegacyStatic<GetSelectedObjectReference>(*vm, "GetSelectedObjectReference");
	bound &= BindLegacyStatic<IsSelectionFullyLoaded>(*vm, "IsSelectionFullyLoaded");
	bound &= BindLegacyStatic<GetFullyLoadedCount>(*vm, "GetFullyLoadedCount");
	bound &= BindLegacyStatic<GetImportedAnimationKind>(*vm, "GetImportedAnimationKind");
	bound &= BindLegacyStatic<GetPatternCount>(*vm, "GetPatternCount");
	bound &= BindLegacyStatic<GetSelectionCount>(*vm, "GetSelectionCount");
	bound &= BindLegacyStatic<GetSelectionWireCount>(*vm, "GetSelectionWireCount");
	bound &= BindLegacyStatic<GetPatternObjects>(*vm, "GetPatternObjects");
	bound &= BindLegacyStatic<GetPatternWires>(*vm, "GetPatternWires");
	bound &= BindLegacyStatic<GetPatternImportWireCount>(*vm, "GetPatternImportWireCount");
	bound &= BindLegacyStatic<GetPatternPlugins>(*vm, "GetPatternPlugins");
	bound &= BindLegacyStatic<GetPatternGeneralInformation>(*vm, "GetPatternGeneralInformation");
	bound &= BindLegacyStatic<GetPatternReferenceInformation>(*vm, "GetPatternReferenceInformation");
	bound &= BindLegacyStatic<IsPositionWithinWorkshop>(*vm, "IsPositionWithinWorkshop");
	bound &= BindLegacyStatic<GetAllConstructibleObjects_>(*vm, "GetAllConstructibleObjects");
	bound &= BindLegacyStatic<GetConstructibleObjectByCreatedObject_>(*vm, "GetConstructibleObjectByCreatedObject");
	bound &= BindLegacyStatic<GetSelectableObjectPool>(*vm, "GetSelectableObjectPool");
	bound &= BindLegacyStatic<GetManualSelectableObjectPool>(*vm, "GetManualSelectableObjectPool");
	bound &= BindLegacyStatic<GetObjectsInBox>(*vm, "GetObjectsInBox");
	bound &= BindLegacyStatic<GetObjectsInCylinder>(*vm, "GetObjectsInCylinder");
	bound &= BindLegacyStatic<GetObjectsInSphere>(*vm, "GetObjectsInSphere");
	bound &= BindLegacyStatic<ApplyShaderEffectToSelection>(*vm, "ApplyShaderEffectToSelection");
	bound &= BindLegacyStatic<RemoveShaderEffectToSelection>(*vm, "RemoveShaderEffectToSelection");
	bound &= BindLegacyStatic<GetKeyName>(*vm, "GetKeyName");
	bound &= BindLegacyStatic<GetText>(*vm, "GetText");
	bound &= BindLegacyStatic<IsOwnedInputAvailable>(*vm, "IsOwnedInputAvailable");
	bound &= BindLegacyStatic<BeginOwnedInput>(*vm, "BeginOwnedInput");
	bound &= BindLegacyStatic<GetOwnedInputState>(*vm, "GetOwnedInputState");
	bound &= BindLegacyStatic<GetOwnedInputResult>(*vm, "GetOwnedInputResult");
	bound &= BindLegacyStatic<IsOwnedInputFinished>(*vm, "IsOwnedInputFinished");
	bound &= BindLegacyStatic<AcknowledgeOwnedInput>(*vm, "AcknowledgeOwnedInput");
	bound &= BindLegacyStatic<CancelOwnedInput>(*vm, "CancelOwnedInput");
	bound &= BindLegacyStatic<AbandonOwnedInput>(*vm, "AbandonOwnedInput");
	bound &= BindLegacyStatic<GetSettingValueString>(*vm, "GetSettingValueString");
	bound &= BindLegacyStatic<GetSettingValueInt>(*vm, "GetSettingValueInt");
	bound &= BindLegacyStatic<GetSettingValueFloat>(*vm, "GetSettingValueFloat");
	bound &= BindLegacyStatic<GetSettingValueBool>(*vm, "GetSettingValueBool");
	bound &= BindLegacyStatic<ReloadSettings>(*vm, "ReloadSettings");
	bound &= BindLegacyStatic<ReportScriptIssue>(*vm, "ReportScriptIssue");
	bound &= BindLegacyStatic<GetSelectionComponentCost>(*vm, "GetSelectionComponentCost");
	bound &= BindLegacyStatic<GetPatternComponentCost>(*vm, "GetPatternComponentCost");
	bound &= BindLegacyStatic<GetComponentCost>(*vm, "GetComponentCost");
	bound &= BindLegacyStatic<CountComponentSources>(*vm, "CountComponentSources");
	bound &= BindLegacyStatic<CountComponentSource>(*vm, "CountComponentSource");
	bound &= BindLegacyStatic<ReportComponentCostTiming>(*vm, "ReportComponentCostTiming");
	bound &= BindLegacyStatic<GetObjectsByCell>(*vm, "GetObjectsByCell");
	bound &= BindLegacyStatic<GetSelectionScaleInputError>(*vm, "GetSelectionScaleInputError");
	bound &= BindLegacyStatic<GetSelectionScaleError>(*vm, "GetSelectionScaleError");
	bound &= BindLegacyStatic<GetObjectsByWorkshopCells>(*vm, "GetObjectsByWorkshopCells");
	bound &= BindLegacyStatic<FilterObjectsByDistance>(*vm, "FilterObjectsByDistance");
	bound &= BindLegacyStatic<FilterObjectsByPlugin>(*vm, "FilterObjectsByPlugin");
	bound &= BindLegacyStatic<Clipboard::Latent::CancelImportedObjects>(*vm, "CancelImportedObjects");
	bound &= BindLegacyStatic<Clipboard::Latent::BeginImportedPowerProgress>(*vm, "BeginImportedPowerProgress");
	bound &= BindLegacyStatic<Clipboard::Latent::AdvanceImportedPowerProgress>(*vm, "AdvanceImportedPowerProgress");
	bound &= BindLegacyStatic<Clipboard::Latent::EndImportedPowerProgress>(*vm, "EndImportedPowerProgress");
	bound &= BindLegacyStatic<Clipboard::Latent::ClearImportedProgress>(*vm, "ClearImportedProgress");
	bound &= BindLegacyStatic<BeginLegacyCleanup>(*vm, "BeginLegacyCleanup");
	bound &= BindLegacyStatic<IsLegacyCleanupActive>(*vm, "IsLegacyCleanupActive");
	bound &= BindLegacyStatic<EndLegacyCleanup>(*vm, "EndLegacyCleanup");
	bound &= BindLegacyStatic<GetLegacyCleanupCandidates>(*vm, "GetLegacyCleanupCandidates");
	bound &= BindLegacyStatic<IsLegacyCleanupReference>(*vm, "IsLegacyCleanupReference");
	if (!bound) {
		logger::critical("One or more ordinary Clipboard Papyrus natives failed to bind");
		return false;
	}

	const Clipboard::Latent::Callbacks latentCallbacks{
		CreateSelectionBoxLatent,
		ScrapSelectionLatent,
		ScrapObjectsLatent,
		SendWorkshopEventToSelectedObjectsLatent,
		EnableObjectsLatent,
		DisableObjectsLatent,
		ScaleSelectionLatent,
		CreateTransmitPowerJob,
		PastePatternObjectsLatent,
		CreatePatternWireJob,
		SnapshotPacedScrapTargets,
		ScrapLatent,
		UseWorkshopThrottling,
		PastePatternObjectsWithReuseLatent,
		EnableWorkshopWaitSnapshots,
		UseSingleWorkshopCallback,
		WorkshopCallbackSpacingMs,
		EnableWorkshopEarlyWaitSnapshot,
		WorkshopCallbackLimit,
		WorkshopCallbackResumeAt,
		TryScaleSelectionLatent
	};
	if (!Clipboard::Latent::Register(*vm, latentCallbacks)) {
		logger::critical("Clipboard latent Papyrus registration failed");
		return false;
	}

	CLIPBOARD_DEBUG_LOG(logger::info("Bound 79 ordinary and 24 latent ClipboardExtension natives"));
	return true;
}

bool F4SEAPI RegisterFuncs(VirtualMachine* vm)
{
	if (!startupCallbackGate.IsEnabled()) {
		return false;
	}
	try {
		return RegisterFuncsImpl(vm);
	} catch (const std::exception& error) {
		try { logger::critical("Clipboard Papyrus registration failed: {}", error.what()); } catch (...) {}
	} catch (...) {
		try { logger::critical("Clipboard Papyrus registration failed with an unknown exception"); } catch (...) {}
	}
	return false;
}

namespace
{
	std::unique_ptr<Clipboard::RuntimeDatabasePreflight::ValidationGuard> runtimeDatabaseGuard;

	class CallbackModuleLease
	{
	public:
		CallbackModuleLease() noexcept
		{
			GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
				reinterpret_cast<LPCWSTR>(&RegisterFuncs), &module);
		}
		~CallbackModuleLease() noexcept
		{
			if (module) {
				FreeLibrary(module);
			}
		}
		CallbackModuleLease(const CallbackModuleLease&) = delete;
		CallbackModuleLease& operator=(const CallbackModuleLease&) = delete;
		[[nodiscard]] bool IsValid() const noexcept { return module != nullptr; }
		void KeepLoaded() noexcept
		{
			// Intentionally retain this extra reference for the process lifetime.
			// F4SE may invoke already-stored callbacks after rejecting this DLL;
			// those callbacks remain resident and their uncommitted gate is inert.
			module = nullptr;
		}

	private:
		HMODULE module{};
	};

	// Runtime Database selects addresses for all three declared families.
	// Keep certification tied to the exact tested binary, not loader metadata.
	[[nodiscard]] constexpr bool SupportsDeclaredLayout(const REL::Version& runtime) noexcept
	{
		return Clipboard::RuntimeCompatibility::SupportsDeclaredLayout(runtime);
	}
	static_assert(SupportsDeclaredLayout(REL::Version{ 1, 11, 137, 0 }));
	static_assert(SupportsDeclaredLayout(REL::Version{ 1, 11, 221, 0 }));
	static_assert(SupportsDeclaredLayout(REL::Version{ 1, 11, 240, 0 }));
	static_assert(SupportsDeclaredLayout(REL::Version{ 1, 11, 4095, 0 }));
	static_assert(SupportsDeclaredLayout(REL::Version{ 1, 10, 163, 0 }));
	static_assert(SupportsDeclaredLayout(REL::Version{ 1, 10, 984, 0 }));

	[[nodiscard]] constexpr F4SE::PluginVersionData MakePluginVersionData() noexcept
	{
		F4SE::PluginVersionData data{};
		data.pluginVersion = pluginVersion;
		for (std::size_t i = 0; i < Version::PROJECT.size() && i < std::size(data.name) - 1; ++i) {
			data.name[i] = Version::PROJECT[i];
		}
		constexpr std::string_view author{ "Everett C Sands / Strucker" };
		for (std::size_t i = 0; i < author.size() && i < std::size(data.author) - 1; ++i) {
			data.author[i] = author[i];
		}
		data.addressIndependence = F4SE::PluginVersionData::kAddressIndependence_Signatures;
		data.structureIndependence = Clipboard::RuntimeCompatibility::kStructureFlags;
		// The value-initialized compatibleVersions array stays empty. Untested
		// Runtimes may attempt resolution; they are not thereby certified.
		return data;
	}
	static_assert([] {
		constexpr auto data = MakePluginVersionData();
		if (data.addressIndependence != F4SE::PluginVersionData::kAddressIndependence_Signatures ||
			data.structureIndependence != Clipboard::RuntimeCompatibility::kStructureFlags) {
			return false;
		}
		for (const auto version : data.compatibleVersions) {
			if (version != 0) {
				return false;
			}
		}
		return true;
	}());

	[[nodiscard]] bool InitializeLogger()
	{
#ifndef NDEBUG
		auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
		auto path = logger::log_directory();
		if (!path) {
			return false;
		}
		*path /= "Clipboard.log";
		// Start each process with a fresh log, including when routine logging is off.
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif
		auto log = std::make_shared<spdlog::logger>("global log", std::make_shared<Clipboard::Logging::Sink>(std::move(sink)));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
		return true;
	}
}

extern "C" __declspec(dllexport) constinit F4SE::PluginVersionData F4SEPlugin_Version = MakePluginVersionData();

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* f4se)
{
	try {
		if (!InitializeLogger()) {
			return false;
		}
		RefreshLoggingSetting();
		const Clipboard::Logging::StartupWindow startupLogging;
		logger::info("----- Clipboard session start -----");
		logger::info("Clipboard build identity: dll internal={} product={}; script reports follow on Clipboard use",
			Version::INTERNAL_BUILD, Version::NAME);
		if (!f4se || f4se->IsEditor()) {
			logger::critical("Refusing to load Clipboard outside the Fallout 4 runtime");
			return false;
		}
		auto preflight = Clipboard::RuntimeDatabasePreflight::ValidateAndHoldRuntimeCandidates(
			std::filesystem::path{ "Data" } / "F4SE" / "Plugins",
			f4se->RuntimeVersion().string());
		if (!preflight) {
			logger::critical("Runtime Database preflight failed: {}", preflight.error);
			return false;
		}
		// The legacy Query export and modern metadata reach the same path.
		if (!SupportsDeclaredLayout(f4se->RuntimeVersion())) {
			logger::critical("Clipboard cannot select a runtime family for {}",
				f4se->RuntimeVersion().string());
			return false;
		}
		const auto recordCount = preflight.recordCount;
		runtimeDatabaseGuard = std::move(preflight.guard);

		F4SE::Init(f4se);
		logger::info("Attempting Runtime Database resolution on {} runtime {}; no executable patch whitelist",
			Clipboard::RuntimeCompatibility::FamilyName(f4se->RuntimeVersion()), f4se->RuntimeVersion().string());
		if (!Clipboard::RuntimeSymbols::ResolveRequiredCommonLibSymbols()) {
			logger::critical("One or more required CommonLib Runtime Database symbols could not be resolved");
			return false;
		}
		if (!Clipboard::EngineAPI::ResolveRequiredSymbols()) {
			logger::critical("One or more required Runtime Database symbols could not be resolved");
			return false;
		}
		Clipboard::Localization::InitializeRuntime(
			std::filesystem::path{ GetRuntimeDirectory() } / "Data" / "F4SE" / "Plugins" / "Clipboard" / "Localization");

		const auto* objectInterface = F4SE::GetObjectInterface();
		const auto* papyrusInterface = F4SE::GetPapyrusInterface();
		const auto* messagingInterface = F4SE::GetMessagingInterface();
		if (!objectInterface || objectInterface->Version() != F4SE::ObjectInterface::kVersion) {
			logger::critical("F4SE Object interface v1 is required for latent operations");
			return false;
		}
		if (!papyrusInterface || !messagingInterface) {
			logger::critical("F4SE Papyrus and Messaging interfaces are required");
			return false;
		}
		CallbackModuleLease callbackModule;
		if (!callbackModule.IsValid()) {
			logger::critical("Could not retain Clipboard while publishing F4SE callbacks");
			return false;
		}
		Clipboard::StartupCallbacks::Registration registration{ startupCallbackGate, callbackModule };
		if (!registration.Attempt([&] { return papyrusInterface->Register(RegisterFuncs); })) {
			logger::critical("Could not register ClipboardExtension Papyrus natives");
			return false;
		}
		if (!registration.Attempt([&] { return messagingInterface->RegisterListener(MessageCallback); })) {
			logger::critical("Could not register the F4SE message listener");
			return false;
		}

		CLIPBOARD_DEBUG_LOG(logger::info(
			"Clipboard {} loaded for Fallout 4 {} with {} Runtime Database records",
			pluginVersionString,
			f4se->RuntimeVersion().string(),
			recordCount));
		registration.Commit();
		return true;
	} catch (const std::exception& error) {
		try { logger::critical("Clipboard load failed: {}", error.what()); } catch (...) {}
	} catch (...) {
		try { logger::critical("Clipboard load failed with an unknown exception"); } catch (...) {}
	}
	return false;
}
