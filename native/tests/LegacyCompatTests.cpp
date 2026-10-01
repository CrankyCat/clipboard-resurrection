#define CLIPBOARD_PAPYRUS_ARRAY_ALGORITHM_ONLY
#include "LegacyPapyrusArray.h"
#include "ImportTransform.h"
#include "MasterPluginBlacklist.h"
#include "OptionalChildOwnership.h"
#include "AttachedScriptCandidates.h"
#include "OptionalFilteringPolicy.h"
#include "OptionalLinkedRefKeywords.h"
#include "PatternWireRows.h"
#include "PatternSourceMovement.h"
#include "ScrapTargetsTests.h"
#include "SettingsNumbers.h"
#include "WorkshopCellScope.h"
#include "WideSelectionDistance.h"

#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>

static bool TestAttachedScriptCandidates()
{
	// Model pooled case-insensitive identities independently of the game heap.
	struct Name {
		std::string value;
		bool operator==(const Name& other) const {
			return Clipboard::OptionalFiltering::Policy::EqualNames(value, other.value);
		}
	};
	struct Type { Name name; const Type* parent; };
	struct Object { const Type* type; bool isConst; bool conditional; bool initialized; };
	Type root{ { "ObjectReference" }, nullptr };
	Type table{ { "ukAutoBeds:ukTablePlaceAtMe" }, &root };
	Type derived{ { "DerivedTable" }, &table };
	Type other{ { "Unrelated" }, &root };
	Object direct{ &table, false, false, true };
	Object inherited{ &derived, true, true, false };
	Object unrelated{ &other, false, false, true };
	Object noType{ nullptr, false, false, false };
	std::unordered_map<std::uint64_t, std::vector<const Object*>> attachments{
		{ 0xFFFF00000001, { &direct } },
		{ 0xFFFF00000002, { &unrelated, &inherited, &direct } },
		{ 0xFFFF00000003, { &unrelated } },
		{ 0xFFFF00000004, { nullptr, &noType } },
		{ 0xFFFF00000005, {} }
	};
	// A large unrelated attachment population must not become parent candidates.
	for (std::uint64_t i = 0; i < 100000; ++i) {
		attachments.emplace(0xFFFF00001000 + i, std::vector<const Object*>{ &unrelated });
	}
	const auto collect = [&](const std::vector<Name>& names, std::size_t maximum = std::numeric_limits<std::size_t>::max()) {
		return Clipboard::OptionalFiltering::CollectAttachedScriptCandidates(attachments, names,
			[](const Object* object) -> const Type* { return object ? object->type : nullptr; },
			[](const Type* type) -> const Name& { return type->name; },
			[](const Type* type) -> const Type* { return type->parent; }, maximum);
	};
	const std::vector<Name> names{ { "UKAUTOBEDS:UKTABLEPLACEATME" } };
	const auto first = collect(names);
	bool limitRejected{};
	try { (void)collect(names, 1); } catch (const std::length_error&) { limitRejected = true; }
	if (!limitRejected || collect(names, 2).size() != 2) {
		std::cerr << "Maintenance snapshot limit returned an unsafe partial inventory\n";
		return false;
	}
	if (std::set(first.begin(), first.end()) != std::set<std::uint64_t>{ 0xFFFF00000001, 0xFFFF00000002 } ||
		first.size() != 2 || !collect({}).empty()) {
		std::cerr << "Attached parent snapshot lost direct/derived/const/conditional/null/name or duplicate boundaries\n";
		return false;
	}
	// Every operation reflects additions, removals, type changes and settings.
	attachments.erase(0xFFFF00000001);
	inherited.type = &other;
	attachments[0xFFFF00000002] = { &inherited };
	attachments[0xFFFF00000006] = { &direct };
	const auto second = collect(names);
	if (second != std::vector<std::uint64_t>{ 0xFFFF00000006 } ||
		!collect(std::vector<Name>{ { "MissingScript" } }).empty()) {
		std::cerr << "Attached parent candidates retained state across operations\n";
		return false;
	}
	return true;
}

static bool TestOptionalLinkedRefKeywords()
{
	using namespace Clipboard::OptionalFiltering;
	constexpr std::uintptr_t configuredSlot = 0x100;
	const auto configured = [](std::uintptr_t key) { return key == configuredSlot; };
	LinkedRefKeywordArray inlineOther{ 0x80000001u, 0, { 0x200, 20 }, 1, 0 };
	LinkedRefKeywordArray inlineMatch{ 0x80000001u, 0, { configuredSlot, 20 }, 1, 0 };
	LinkedRefKeywordArray empty{ 0x80000000u, 0, {}, 0, 0 };
	std::array<std::array<std::uintptr_t, 2>, 2> heapEntries{ { { 0x200, 20 }, { configuredSlot, 21 } } };
	LinkedRefKeywordArray heap{ 2, 0, { reinterpret_cast<std::uintptr_t>(heapEntries.data()), 0 }, 2, 0 };
	if (MayContainConfiguredChildSlot(&inlineOther, false, configured) ||
		MayContainConfiguredChildSlot(&empty, false, configured) ||
		!MayContainConfiguredChildSlot(&inlineMatch, false, configured) ||
		!MayContainConfiguredChildSlot(&heap, false, configured) ||
		!MayContainConfiguredChildSlot(&inlineOther, true, configured)) {
		std::cerr << "Linked-reference keyword prefilter lost inline/heap or alias boundaries\n";
		return false;
	}
	// Suspicious payloads cannot become new ownership exclusions or bypasses.
	for (const LinkedRefKeywordArray malformed : {
			LinkedRefKeywordArray{ 0x80000002u, 0, {}, 2, 0 },
			LinkedRefKeywordArray{ 1, 0, {}, 2, 0 },
			LinkedRefKeywordArray{ 1, 0, {}, 1, 0 },
			LinkedRefKeywordArray{ 65537, 0, {}, 65537, 0 } }) {
		if (!MayContainConfiguredChildSlot(&malformed, false, configured)) {
			std::cerr << "Malformed keyword storage did not retain engine-query fallback\n";
			return false;
		}
	}
	if (!MayContainConfiguredChildSlot(nullptr, false, configured)) {
		return false;
	}

	// Model the current log's 55,346-reference world: unrelated workshop links
	// must not query the VM, while direct-slot and alias owners remain identical.
	struct Parent
	{
		LinkedRefKeywordArray links;
		bool aliases;
		bool supportedScript;
		std::uint32_t child;
	};
	std::vector<Parent> parents(55344, Parent{ inlineOther, false, false, 0 });
	parents.push_back({ inlineMatch, false, true, 41 });
	parents.push_back({ inlineOther, true, true, 42 });
	std::vector<const Parent*> represented;
	for (const auto& parent : parents) {
		represented.push_back(&parent);
	}
	std::size_t beforeVMQueries = 0;
	std::size_t afterVMQueries = 0;
	ChildOwnershipIndex<const Parent*, std::uintptr_t> before;
	ChildOwnershipIndex<const Parent*, std::uintptr_t> after;
	const auto child = [](const Parent* parent, std::uintptr_t) -> std::optional<std::uint32_t> {
		return parent->child ? std::optional{ parent->child } : std::nullopt;
	};
	const std::array keywords{ configuredSlot };
	before.Build(represented, keywords, [&](const Parent* parent) {
		++beforeVMQueries;
		return parent->supportedScript;
	}, child);
	after.Build(represented, keywords, [&](const Parent* parent) {
		if (!MayContainConfiguredChildSlot(&parent->links, parent->aliases, configured)) {
			return false;
		}
		++afterVMQueries;
		return parent->supportedScript;
	}, child);
	for (std::uint32_t id = 40; id <= 43; ++id) {
		const auto current = [=](const Parent* parent, std::uintptr_t) { return parent->child == id; };
		if (before.Matches(id, current) != after.Matches(id, current)) {
			std::cerr << "Keyword prefilter changed direct/alias child ownership\n";
			return false;
		}
	}
	if (beforeVMQueries != 55346 || afterVMQueries != 2) {
		std::cerr << "Unrelated workshop slots still perform whole-world VM queries\n";
		return false;
	}
	return true;
}

static bool TestOptionalChildOwnership()
{
	// Exercise the production index with fake native boundaries. These callbacks
	// model script/linked-ref lookups; they do not certify the engine adapters.
	struct Reference
	{
		std::uint32_t id;
		std::uint32_t base;
		bool supportedScript;
		std::unordered_map<std::uint32_t, Reference*> outgoing;
	};
	using Parent = std::shared_ptr<Reference>;
	using Index = Clipboard::OptionalFiltering::ChildOwnershipIndex<Parent, std::uint32_t>;
	constexpr std::uint32_t tableSlot = 3;
	constexpr std::uint32_t lampSlot = 8;
	constexpr std::uint32_t unconfiguredSlot = 99;
	Reference table{ 1, 10, false, {} };
	Reference independentlyPlacedSameBase{ 2, table.base, false, {} };
	Reference unsupportedParentChild{ 3, 11, false, {} };
	Reference unconfiguredSlotChild{ 4, 12, false, {} };
	Reference replacement{ 5, table.base, false, {} };
	auto parentOutsideSelection = std::make_shared<Reference>(Reference{ 100, 20, true, {} });
	auto alternateParent = std::make_shared<Reference>(Reference{ 101, 20, true, {} });
	auto unsupportedParent = std::make_shared<Reference>(Reference{ 102, 20, false, {} });
	parentOutsideSelection->outgoing = {
		{ tableSlot, &table }, { lampSlot, nullptr }, { unconfiguredSlot, &unconfiguredSlotChild }
	};
	alternateParent->outgoing = {
		{ tableSlot, &table }, { lampSlot, alternateParent.get() }
	};
	unsupportedParent->outgoing = { { tableSlot, &unsupportedParentChild } };
	const std::vector<Parent> representedParents{ nullptr, parentOutsideSelection, unsupportedParent, alternateParent };
	const std::vector<std::uint32_t> configuredKeywords{ tableSlot, lampSlot };
	const auto isSupportedParent = [](const Parent& parent) { return parent && parent->supportedScript; };
	const auto linkedChild = [](const Parent& parent, std::uint32_t keyword) -> Reference* {
		if (!parent) {
			return nullptr;
		}
		const auto found = parent->outgoing.find(keyword);
		return found == parent->outgoing.end() ? nullptr : found->second;
	};
	const auto childID = [&](const Parent& parent, std::uint32_t keyword) -> std::optional<std::uint32_t> {
		const auto* child = linkedChild(parent, keyword);
		return child && child != parent.get() ? std::optional{ child->id } : std::nullopt;
	};
	const auto matches = [&](const Index& index, const Reference* candidate) {
		return index.Matches(candidate->id, [&](const Parent& parent, std::uint32_t keyword) {
			return linkedChild(parent, keyword) == candidate;
		});
	};
	Index firstScan;
	firstScan.Build(representedParents, configuredKeywords, isSupportedParent, childID);
	// The owning root is outside the selected/candidate set, yet its actual child
	// is excluded. No base-form inference may exclude the independent copy/root.
	if (!matches(firstScan, &table) || matches(firstScan, &independentlyPlacedSameBase) ||
		matches(firstScan, parentOutsideSelection.get()) || matches(firstScan, alternateParent.get()) ||
		matches(firstScan, &unsupportedParentChild) || matches(firstScan, &unconfiguredSlotChild) ||
		matches(firstScan, &replacement)) {
		std::cerr << "Optional ownership lost parent/slot boundaries or excluded an independent/root reference\n";
		return false;
	}
	// One owner can retarget while another still owns the child. Every retained
	// matching link must be checked, and stale positive membership must disappear.
	parentOutsideSelection->outgoing[tableSlot] = &replacement;
	if (!matches(firstScan, &table)) {
		std::cerr << "A stale first owner concealed another current owner of the generated child\n";
		return false;
	}
	alternateParent->outgoing[tableSlot] = nullptr;
	if (matches(firstScan, &table)) {
		std::cerr << "Optional ownership retained an exclusion after all current links were removed\n";
		return false;
	}
	Index nextScan;
	nextScan.Build(representedParents, configuredKeywords, isSupportedParent, childID);
	if (!matches(nextScan, &replacement) || matches(nextScan, &table) ||
		matches(nextScan, &independentlyPlacedSameBase)) {
		std::cerr << "A new ownership scan did not rebuild current child identity\n";
		return false;
	}
		nextScan.Build(representedParents, std::vector<std::uint32_t>{}, isSupportedParent, childID);
	if (matches(nextScan, &replacement)) {
		std::cerr << "Clearing optional slot configuration retained previous ownership exclusions\n";
		return false;
	}

	// The production index stores NiPointer parents. The shared algorithm must
	// likewise keep its retaining Parent type alive after the input snapshot dies,
	// and release that ownership when the operation ends.
	std::weak_ptr<Reference> retainedParent;
	{
		Index lifetimeScan;
		{
			auto transientParent = std::make_shared<Reference>(Reference{ 103, 20, true, { { tableSlot, &table } } });
			retainedParent = transientParent;
			lifetimeScan.Build(std::vector<Parent>{ transientParent }, configuredKeywords, isSupportedParent, childID);
		}
		if (retainedParent.expired() || !matches(lifetimeScan, &table)) {
			std::cerr << "Ownership index did not retain a parent after its input snapshot was released\n";
			return false;
		}
		lifetimeScan.Build(std::vector<Parent>{}, configuredKeywords, isSupportedParent, childID);
		if (!retainedParent.expired() || matches(lifetimeScan, &table)) {
			std::cerr << "Rebuilding an empty parent snapshot kept stale references or child membership\n";
			return false;
		}
		{
			auto operationParent = std::make_shared<Reference>(Reference{ 104, 20, true, { { tableSlot, &table } } });
			retainedParent = operationParent;
			lifetimeScan.Build(std::vector<Parent>{ operationParent }, configuredKeywords, isSupportedParent, childID);
		}
	}
	if (!retainedParent.expired()) {
		std::cerr << "Ending an ownership operation retained its parent snapshot\n";
		return false;
	}
	return true;
}

static bool TestImportTransforms()
{
	struct Point { float x{}, y{}, z{}; };
	using Clipboard::ImportTransform::Compose;
	const Point origin{ 100, 200, 300 };
	const Point rotation{ 0.1F, 0.2F, 1.57079632679F };
	const Point local{ 10, 20, 30 };
	const Point localRotation{ 0.3F, 0.4F, 0.5F };
	Point position{}, angle{};
	if (!Compose(origin, rotation, local, localRotation, position, angle) ||
		std::abs(position.x - 120) > 0.0001F || std::abs(position.y - 190) > 0.0001F ||
		position.z != 330 || angle.x != localRotation.x || angle.y != localRotation.y ||
		std::abs(angle.z - (rotation.z + localRotation.z)) > 0.0001F) {
		std::cerr << "Import transform changed tool-relative position or absolute X/Y rotation\n";
		return false;
	}
	// Engine reference location is NiPoint3A, while rotations and movement
	// outputs are NiPoint3. Their alignment must not force identical types.
	struct alignas(16) AlignedPoint : Point { float padding{}; };
	Point mixedPosition{}, mixedRotation{};
	if (!Compose(AlignedPoint{ origin }, rotation, local, localRotation, mixedPosition, mixedRotation) ||
		mixedPosition.x != position.x || mixedPosition.y != position.y || mixedPosition.z != position.z ||
		mixedRotation.x != angle.x || mixedRotation.y != angle.y || mixedRotation.z != angle.z) {
		std::cerr << "Import transform lost mixed aligned position and unaligned rotation support\n";
		return false;
	}
	// A caller can reuse its local values as outputs, as the live import does.
	Point aliasedPosition = local, aliasedRotation = localRotation;
	if (!Compose(origin, rotation, aliasedPosition, aliasedRotation, aliasedPosition, aliasedRotation) ||
		aliasedPosition.x != position.x || aliasedPosition.y != position.y || aliasedPosition.z != position.z ||
		aliasedRotation.z != angle.z) {
		std::cerr << "Import transform lost its in-place output contract\n";
		return false;
	}
	for (const float invalid : { std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
		for (std::size_t point = 0; point < 4; ++point) {
			for (std::size_t axis = 0; axis < 3; ++axis) {
				Point inputs[]{ origin, rotation, local, localRotation };
				float* coordinate[]{ &inputs[point].x, &inputs[point].y, &inputs[point].z };
				*coordinate[axis] = invalid;
				Point rejectedPosition{ 1, 2, 3 }, rejectedRotation{ 4, 5, 6 };
				if (Compose(inputs[0], inputs[1], inputs[2], inputs[3], rejectedPosition, rejectedRotation) ||
					rejectedPosition.x != 1 || rejectedPosition.y != 2 || rejectedPosition.z != 3 ||
					rejectedRotation.x != 4 || rejectedRotation.y != 5 || rejectedRotation.z != 6) {
					std::cerr << "Non-finite import input escaped validation or changed rejected outputs\n";
					return false;
				}
			}
		}
	}
	const float maximum = std::numeric_limits<float>::max();
	if (Compose(Point{ maximum, 0, 0 }, Point{}, Point{ maximum, 0, 0 }, Point{}, position, angle) ||
		Compose(Point{}, Point{ 0, 0, maximum }, Point{}, Point{ 0, 0, maximum }, position, angle) ||
		Compose(Point{}, Point{ 0, 0, -0.78539816339F }, Point{ maximum, -maximum, 0 }, Point{}, position, angle)) {
		std::cerr << "Finite import operands overflowed into an engine transform\n";
		return false;
	}
	return true;
}

static bool TestSettingsNumbers()
{
	using Clipboard::SettingsNumbers::Int;
	using Clipboard::SettingsNumbers::Float;
	if (Int(" -1 ", 7) != 0xFFFFFFFFU || Int("-2147483648", 7) != 0x80000000U ||
		Int("+2147483647", 7) != 0x7FFFFFFFU || Int("\t0\r\n", 7) != 0 ||
		Int("00123", 7) != 123 || Float(" +1.25e2 ", 7) != 125.0F ||
		Float("-.5", 7) != -0.5F || Float("0", 7) != 0) {
		std::cerr << "Numeric settings changed valid signed int bits or decimal float values\n";
		return false;
	}
	for (const std::string_view invalid : { "", " \t", "+", "-", "++1", "+-1", "1suffix",
		"2147483648", "-2147483649", "4294967295", "999999999999999999999", "1.5" }) {
		if (Int(invalid, 0xFFFFFFFEU) != 0xFFFFFFFEU) {
			std::cerr << "Invalid integer setting did not return the caller default\n";
			return false;
		}
	}
	for (const std::string_view invalid : { "", " \t", "+", "-", "++1", "+-1", "1suffix",
		"nan", "NaN", "inf", "-infinity", "1e1000", "1e-1000" }) {
		if (Float(invalid, 6.5F) != 6.5F) {
			std::cerr << "Invalid or non-finite float setting did not return the caller default\n";
			return false;
		}
	}
	if (Int(std::string_view{ "1\0junk", 6 }, 7) != 7 || Float(std::string_view{ "1\0junk", 6 }, 7) != 7) {
		std::cerr << "Embedded NUL silently truncated numeric settings\n";
		return false;
	}
	return true;
}

int main()
{
	if (!TestImportTransforms() || !TestSettingsNumbers() || !Clipboard::Scrapping::Tests::TestRetainedScrapTargets()) {
		return 1;
	}
	// Adjacent/persistent exterior cells may share a workshop. Coordinates in
	// another world/interior must never reach the engine's spatial-only check.
	using Clipboard::SourceMovement::Space;
	using Clipboard::SourceMovement::IsWithinWorkshop;
	int boundaryCalls = 0;
	const auto boundary = [&boundaryCalls](float x, float y, float z) {
		++boundaryCalls;
		return x >= -12000.0F && x <= 12000.0F && y >= -9000.0F && y <= 9000.0F && z >= -500.0F && z <= 3000.0F;
	};
	const Space workshopSpace{ 1, false, 10 };
	if (!IsWithinWorkshop({ 2, false, 10 }, workshopSpace, 10000, 0, 0, boundary) ||
		!IsWithinWorkshop({ 3, false, 10 }, workshopSpace, -10000, 0, 0, boundary) ||
		IsWithinWorkshop({ 1, false, 10 }, workshopSpace, 50000, 0, 0, boundary) ||
		IsWithinWorkshop({ 1, false, 10 }, workshopSpace, 0, 0, 9000, boundary) ||
		!IsWithinWorkshop({ 5, true, 0 }, { 5, true, 0 }, 0, 0, 0, boundary)) {
		std::cerr << "Source movement did not follow the workshop's actual build volumes\n";
		return 1;
	}
	const int callsBeforeInvalidSpace = boundaryCalls;
	if (IsWithinWorkshop({ 2, false, 11 }, workshopSpace, 0, 0, 0, boundary) ||
		IsWithinWorkshop({ 0, false, 10 }, workshopSpace, 0, 0, 0, boundary) ||
		IsWithinWorkshop({ 2, false, 0 }, { 1, false, 0 }, 0, 0, 0, boundary) ||
		IsWithinWorkshop({ 5, true, 0 }, workshopSpace, 0, 0, 0, boundary) ||
		IsWithinWorkshop({ 5, true, 0 }, { 6, true, 0 }, 0, 0, 0, boundary) ||
		IsWithinWorkshop({ 2, false, 10 }, workshopSpace, std::numeric_limits<float>::quiet_NaN(), 0, 0, boundary) ||
		IsWithinWorkshop({ 2, false, 10 }, workshopSpace, 0, std::numeric_limits<float>::infinity(), 0, boundary) ||
		IsWithinWorkshop({ 2, false, 10 }, workshopSpace, 0, 0, 1000000010.0F, boundary) ||
		boundaryCalls != callsBeforeInvalidSpace) {
		std::cerr << "Invalid source movement space/coordinates reached the engine boundary check\n";
		return 1;
	}
	if (!TestAttachedScriptCandidates() || !TestOptionalLinkedRefKeywords() || !TestOptionalChildOwnership()) {
		return 1;
	}
	// Exercise the same direct-master walker as the TESFile adapter without
	// loading the game or allocating engine-owned containers.
	struct PluginFile
	{
		std::string_view name;
		std::uint8_t compileIndex{ 0 };
		std::uint32_t masterCount{ 0 };
		PluginFile** masterPtrs{ nullptr };
		bool IsActive() const { return compileIndex != 0xFF; }
		std::string_view GetFilename() const { return name; }
	};
	PluginFile fallout{ "Fallout4.esm" };
	PluginFile ss2{ "SS2.esm" };
	PluginFile simSettlements{ "SimSettlements.esm" };
	PluginFile nearMatch{ "NotSS2.esm" };
	PluginFile* ss2Masters[]{ &fallout, nullptr, &ss2 };
	PluginFile* ss1Masters[]{ &fallout, &simSettlements };
	PluginFile* unrelatedMasters[]{ &fallout, &nearMatch };
	PluginFile addon{ "SS2Addon.esp", 5, 3, ss2Masters };
	PluginFile lightAddon{ "SS1Addon.esl", 0xFE, 2, ss1Masters };
	PluginFile inactiveAddon{ "Disabled.esp", 0xFF, 3, ss2Masters };
	PluginFile unrelated{ "Unrelated.esp", 6, 2, unrelatedMasters };
	PluginFile absentTable{ "NoTable.esp", 7, 3 };
	PluginFile* indirectMasters[]{ &fallout, &addon };
	PluginFile indirect{ "Indirect.esp", 8, 2, indirectMasters };
	const std::vector<std::string> masterBlacklist{ "sS2.EsM", "simsettlements.ESM", "" };
	using Clipboard::Blacklist::FindBlacklistedMaster;
	if (FindBlacklistedMaster(&addon, masterBlacklist) != "SS2.esm" ||
		FindBlacklistedMaster(&lightAddon, masterBlacklist) != "SimSettlements.esm" ||
		!FindBlacklistedMaster(&inactiveAddon, masterBlacklist).empty() ||
		!FindBlacklistedMaster(&unrelated, masterBlacklist).empty() ||
		!FindBlacklistedMaster(&absentTable, masterBlacklist).empty() ||
		!FindBlacklistedMaster(static_cast<const PluginFile*>(nullptr), masterBlacklist).empty() ||
		!FindBlacklistedMaster(&ss2, masterBlacklist).empty() ||
		!FindBlacklistedMaster(&indirect, masterBlacklist).empty() ||
		!FindBlacklistedMaster(&addon, {}).empty() ||
		!FindBlacklistedMaster(&addon, std::vector<std::string>{ "SS2", "SS2.esm.extra" }).empty() ||
		FindBlacklistedMaster(&indirect, std::vector<std::string>{ "ss2addon.ESP" }) != "SS2Addon.esp") {
		std::cerr << "Direct master blacklisting changed root, inactive, light, or exact-name semantics\n";
		return 1;
	}

	using Clipboard::OptionalFiltering::Policy::MatchesPluginFamily;
	using Clipboard::OptionalFiltering::Policy::ParseFormSpec;
	// Optional collection families deliberately include the named root, unlike
	// the old blanket master blacklist. Neither light files nor missing master
	// tables change that rule, and only a direct active dependency can expand it.
	PluginFile lightRoot{ "Root.esl", 0xFE };
	PluginFile disabledRoot{ "SS2.esm", 0xFF };
	PluginFile* disabledMasters[]{ nullptr, &disabledRoot };
	PluginFile disabledMasterChild{ "BadDependency.esp", 9, 2, disabledMasters };
	PluginFile* mixedMasters[]{ &disabledRoot, &simSettlements };
	PluginFile mixedMasterChild{ "MixedDependency.esl", 0xFE, 2, mixedMasters };
	if (!MatchesPluginFamily(&ss2, masterBlacklist) ||
		!MatchesPluginFamily(&addon, masterBlacklist) ||
		!MatchesPluginFamily(&lightAddon, masterBlacklist) ||
		!MatchesPluginFamily(&lightRoot, std::vector<std::string>{ "root.ESL" }) ||
		!MatchesPluginFamily(&mixedMasterChild, masterBlacklist) ||
		MatchesPluginFamily(&disabledRoot, masterBlacklist) ||
		MatchesPluginFamily(&disabledMasterChild, masterBlacklist) ||
		MatchesPluginFamily(&inactiveAddon, masterBlacklist) ||
		MatchesPluginFamily(&nearMatch, masterBlacklist) ||
		MatchesPluginFamily(&unrelated, masterBlacklist) ||
		MatchesPluginFamily(&absentTable, masterBlacklist) ||
		MatchesPluginFamily(static_cast<const PluginFile*>(nullptr), masterBlacklist) ||
		MatchesPluginFamily(&indirect, masterBlacklist) ||
		MatchesPluginFamily(&ss2, {}) ||
		MatchesPluginFamily(&addon, std::vector<std::string>{ "SS2", "SS2.esm.extra" }) ||
		!MatchesPluginFamily(&indirect, std::vector<std::string>{ "ss2addon.ESP" })) {
		std::cerr << "Optional collection families lost exact roots, active direct dependencies, or light-plugin support\n";
		return 1;
	}

	const auto parsedMarker = ParseFormSpec(" \tSS2.esm  #  00084480\r\n");
	const auto parsedSpacedPlugin = ParseFormSpec("[SS2 Addon] Example.esp#1");
	const auto parsedFullBoundary = ParseFormSpec("Example.esp#16777215");
	const auto parsedLightBoundary = ParseFormSpec("Example.esl#4095");
	// Plugin light/full identity is known only during resolution. The parser
	// enforces the shared 24-bit local-ID ceiling, not an extension heuristic.
	const auto parsedLargerEsl = ParseFormSpec("Example.esl#4096");
	if (!parsedMarker || parsedMarker->plugin != "SS2.esm" || parsedMarker->localFormID != 0x014A00 ||
		!parsedSpacedPlugin || parsedSpacedPlugin->plugin != "[SS2 Addon] Example.esp" || parsedSpacedPlugin->localFormID != 1 ||
		!parsedFullBoundary || parsedFullBoundary->localFormID != 0x00FFFFFF ||
		!parsedLightBoundary || parsedLightBoundary->localFormID != 0xFFF ||
		!parsedLargerEsl || parsedLargerEsl->localFormID != 4096) {
		std::cerr << "Optional form specifications lost trimming, decimal interpretation, or valid local-ID boundaries\n";
		return 1;
	}
	for (const std::string_view invalid : {
		"", " \t", "SS2.esm", "#1", " \t#1", "SS2.esm#", "SS2.esm# \t",
		"SS2.esm#0", "SS2.esm#0000", "SS2.esm#-1", "SS2.esm#+1", "SS2.esm#0x14A00",
		"SS2.esm#84480junk", "SS2.esm#1 2", "SS2.esm#1.0", "SS2.esm#1#2", "SS2.esm##1",
		"SS2.esm#16777216", "SS2.esm#4294967295", "SS2.esm#4294967296", "SS2.esm#999999999999999999999999"
	}) {
		if (ParseFormSpec(invalid)) {
			std::cerr << "Malformed optional form specification was accepted: " << invalid << '\n';
			return 1;
		}
	}
	if (ParseFormSpec(std::string_view{ "SS2.esm\0#7", 10 })) {
		std::cerr << "Embedded NUL could truncate the configured optional plugin identity\n";
		return 1;
	}

	// Parse the exact production defaults: all audited parent slots must remain
	// unique valid decimal keys, including the three links established live.
	std::string_view remaining = Clipboard::OptionalFiltering::kDefaultChildLinkKeywords;
	std::set<std::uint32_t> configuredSlots;
	while (!remaining.empty()) {
		const auto separator = remaining.find(',');
		const auto item = ParseFormSpec(remaining.substr(0, separator));
		if (!item || item->plugin != "ukAutoBeds.esp" || !configuredSlots.insert(item->localFormID).second) {
			std::cerr << "Default Auto Beds slot CSV contains an invalid, foreign, or duplicate keyword\n";
			return 1;
		}
		remaining = separator == std::string_view::npos ? std::string_view{} : remaining.substr(separator + 1);
	}
	if (configuredSlots.size() != 42 || !configuredSlots.contains(0x0965C5) ||
		!configuredSlots.contains(0x0E34F3) || !configuredSlots.contains(0x051EF2) ||
		!configuredSlots.contains(0x0090AE) || !configuredSlots.contains(0x178F2F)) {
		std::cerr << "Default Auto Beds slots lost audited coverage or a live-verified child link\n";
		return 1;
	}

	using Clipboard::WideSelection::Counts;
	using Clipboard::WideSelection::Distance;
	using Clipboard::WideSelection::Filter;
	using Clipboard::WideSelection::UsesPersistentCellLimit;
	int persistentCellToken = 0;
	int ordinaryCellToken = 0;
	if (!UsesPersistentCellLimit(false, &persistentCellToken, &persistentCellToken) ||
		UsesPersistentCellLimit(false, &ordinaryCellToken, &persistentCellToken) ||
		UsesPersistentCellLimit(true, &persistentCellToken, &persistentCellToken) ||
		UsesPersistentCellLimit(false, nullptr, nullptr) ||
		UsesPersistentCellLimit(false, &ordinaryCellToken, nullptr)) {
		std::cerr << "Persistent storage was confused with an ordinary or unknown cell\n";
		return 1;
	}
	struct ScopedDistance
	{
		double distance;
		bool persistentStorage;
	};
	const VMArray<ScopedDistance> mixedCells{
		{ 4999.0, true }, { 5000.0, true }, { 5000.01, true },
		{ 5000.01, false }, { 90000.0, false }, { -1.0, false }
	};
	Counts scopedCounts;
	const auto mixedKept = Filter<VMArray<ScopedDistance>>(mixedCells, 5000.0,
		[](const ScopedDistance& row) { return row.distance; },
		[](const ScopedDistance& row) { return row.persistentStorage; }, scopedCounts);
	if (mixedKept.size() != 4 || mixedKept[1].distance != 5000.0 ||
		mixedKept[2].persistentStorage || mixedKept[3].distance != 90000.0 ||
		scopedCounts.persistentCandidates != 3 || scopedCounts.uncappedRetained != 2 ||
		scopedCounts.outOfRange != 1 || scopedCounts.invalidDistance != 1) {
		std::cerr << "The 5000-unit cap must affect only persistent-cell references\n";
		return 1;
	}
	const auto invalidMaximumKept = Filter<VMArray<ScopedDistance>>(mixedCells,
		std::numeric_limits<double>::quiet_NaN(),
		[](const ScopedDistance& row) { return row.distance; },
		[](const ScopedDistance& row) { return row.persistentStorage; }, scopedCounts);
	if (invalidMaximumKept.size() != 2 || scopedCounts.uncappedRetained != 2 || scopedCounts.invalidDistance != 4) {
		std::cerr << "Invalid persistent-cell limit unexpectedly blocked valid ordinary cells\n";
		return 1;
	}
	// Reproduce the 635-candidate runtime report without Papyrus Array.Add.
	// Include the first post-128 row and the final row, preserving order.
	for (const std::uint32_t size : { 127U, 128U, 129U, 635U }) {
		VMArray<std::uint32_t> candidates;
		for (std::uint32_t i = 0; i < size; ++i) {
			candidates.push_back(i);
		}
		Counts counts;
		const auto kept = Filter<VMArray<std::uint32_t>>(candidates, 10000.0,
			[](std::uint32_t i) { return i < 13 ? 10001.0 : 431.84; }, counts);
		if (kept.size() != size - 13 || kept[0] != 13 || kept[kept.size() - 1] != size - 1 ||
			counts.input != size || counts.retained != size - 13 || counts.outOfRange != 13 ||
			counts.invalidDistance != 0) {
			std::cerr << "Wide distance filtering truncated or reordered candidates at the Papyrus growth limit\n";
			return 1;
		}
		for (std::size_t i = 0; i < kept.size(); ++i) {
			if (kept[i] != i + 13) {
				std::cerr << "Wide distance compaction changed instance order\n";
				return 1;
			}
		}
	}
	Counts edgeCounts;
	const VMArray<double> distances{ 0.0, 10000.0, 10000.01, -1.0,
		std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() };
	const auto edgeRows = Filter<VMArray<double>>(distances, 10000.0,
		[](double distance) { return distance; }, edgeCounts);
	if (edgeRows.size() != 2 || edgeRows[1] != 10000.0 || edgeCounts.outOfRange != 1 ||
		edgeCounts.invalidDistance != 3 ||
		Distance(true, { 0, 0, 0 }, { 6000, 0, 8000 }) != 10000.0 ||
		Distance(false, { 0, 0, 0 }, { 0, 0, 0 }) != -1.0 ||
		Distance(true, { 0, 0, std::numeric_limits<double>::infinity() }, { 0, 0, 0 }) != -1.0 ||
		Distance(true, { 0, 0, 0 }, { std::numeric_limits<double>::quiet_NaN(), 0, 0 }) != -1.0) {
		std::cerr << "Wide distance boundary, space or invalid-coordinate guard failed\n";
		return 1;
	}
	for (const double badMaximum : { -1.0, std::numeric_limits<double>::infinity(),
		std::numeric_limits<double>::quiet_NaN() }) {
		if (!Filter<VMArray<double>>(distances, badMaximum,
			[](double distance) { return distance; }, edgeCounts).empty() || edgeCounts.invalidDistance != distances.size()) {
			std::cerr << "Invalid distance setting admitted candidates\n";
			return 1;
		}
	}
	VMArray<int*> nullableRows{ nullptr };
	if (!Filter<VMArray<int*>>(nullableRows, 10000.0,
		[](const int* value) { return value ? 0.0 : -1.0; }, edgeCounts).empty() || edgeCounts.invalidDistance != 1) {
		std::cerr << "Null distance candidate was not rejected\n";
		return 1;
	}

	using Clipboard::WorkshopScope::Membership;
	using Clipboard::WorkshopScope::ResolveMembership;
	using Clipboard::WorkshopScope::ExteriorCellKey;
	// A created bed missing from a cell's reference array still has its live
	// owner. Neither the base signature nor a recipe enters cell membership.
	if (ResolveMembership(true, 42, 0, 0, true) != Membership::kOwnerFallback ||
		ResolveMembership(true, 42, 42, 0, true) != Membership::kLocation ||
		ResolveMembership(true, 42, 42, 0, false) != Membership::kLocation ||
		ResolveMembership(true, 42, 0, 42, false) != Membership::kLocation) {
		std::cerr << "Workshop scope lost direct children or location-matching authored/persistent objects\n";
		return 1;
	}
	// A different known location wins over ownership. A persistent parent with
	// no location evidence cannot admit all of a worldspace, even permissively.
	if (ResolveMembership(true, 42, 43, 42, true) != Membership::kOutside ||
		ResolveMembership(true, 42, 0, 43, true) != Membership::kOutside ||
		ResolveMembership(true, 42, 0, 0, false) != Membership::kOutside ||
		ResolveMembership(false, 42, 42, 42, true) != Membership::kOutside ||
		ResolveMembership(true, 0, 42, 0, true) != Membership::kOutside ||
		ResolveMembership(true, 0, 0, 0, false) != Membership::kOutside ||
		ResolveMembership(true, 42, 42, 43, false) != Membership::kLocation) {
		std::cerr << "Workshop ownership or missing location widened the exact cell scope\n";
		return 1;
	}
	if (ExteriorCellKey(0.0f, 4095.0f) != 0 ||
		ExteriorCellKey(4096.0f, 8192.0f) != 0x00010002 ||
		ExteriorCellKey(-0.1f, -4096.0f) != -1 ||
		ExteriorCellKey(-4096.1f, 0.0f) != -131072 ||
		ExteriorCellKey(std::numeric_limits<float>::infinity(), 0.0f).has_value() ||
		ExteriorCellKey(0.0f, std::numeric_limits<float>::quiet_NaN()).has_value() ||
		ExteriorCellKey(134217728.0f, 0.0f).has_value()) {
		std::cerr << "Exterior persistent-reference coordinates mapped to the wrong cell\n";
		return 1;
	}

	VMArray<void*> rows;
	rows.push_back(static_cast<void*>(nullptr));

	if (rows.Length() != 1) {
		std::cerr << "A null pattern-object placeholder did not retain its row\n";
		return 1;
	}

	void* value = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
	if (!rows.Get(&value, 0) || value != nullptr) {
		std::cerr << "The retained null pattern-object placeholder could not be read back\n";
		return 1;
	}

	void* validValue = nullptr;
	rows.Push(&validValue);
	if (rows.Length() != 2) {
		std::cerr << "VMArray::Push no longer appends an explicit pointer value\n";
		return 1;
	}

	struct HostReference
	{
		std::uint32_t row;
	};
	HostReference references[]{ { 0 }, { 1 }, { 2 }, { 3 }, { 4 }, { 5 }, { 6 } };
	struct PapyrusObjectRow
	{
		bool isNone;
		HostReference* reference;
	};
	const PapyrusObjectRow papyrusRows[]{
		{ false, &references[0] },
		{ true, nullptr },
		{ false, &references[2] },
		{ false, &references[3] }
	};
	std::uint32_t resolveCalls = 0;
	const auto unpackedRows = Clipboard::PapyrusArray::UnpackObjectRows<VMArray<HostReference*>>(
		papyrusRows,
		[](const PapyrusObjectRow& row) { return row.isNone; },
		[&resolveCalls](const PapyrusObjectRow& row) {
			++resolveCalls;
			return row.reference;
		});
	if (unpackedRows.Length() != 4 || resolveCalls != 3 ||
		unpackedRows[0] != &references[0] || unpackedRows[1] != nullptr ||
		unpackedRows[2] != &references[2] || unpackedRows[3] != &references[3]) {
		std::cerr << "Papyrus ObjectReference[] unpacking did not preserve None and ordinary rows\n";
		return 1;
	}

	VMArray<HostReference*> placedRows;
	for (auto& reference : references) {
		auto* valueAtRow = &reference;
		placedRows.Push(&valueAtRow);
	}
	placedRows.push_back(static_cast<HostReference*>(nullptr));

	HostReference* first = nullptr;
	HostReference* second = nullptr;
	using Clipboard::PatternRows::ResolveWireEndpoints;
	using Clipboard::PatternRows::WireEndpointStatus;
	if (ResolveWireEndpoints(placedRows, 5, 4, first, second) != WireEndpointStatus::kReady ||
		first != &references[5] || second != &references[4]) {
		std::cerr << "A wire before a filtered row did not retain its physical row endpoints\n";
		return 1;
	}
	if (ResolveWireEndpoints(placedRows, 6, 5, first, second) != WireEndpointStatus::kReady ||
		first != &references[6] || second != &references[5]) {
		std::cerr << "A second wire before a filtered row did not retain its physical row endpoints\n";
		return 1;
	}
	if (ResolveWireEndpoints(placedRows, 7, 5, first, second) != WireEndpointStatus::kFirstRowMissing) {
		std::cerr << "A wire endpoint on a filtered row was not rejected independently\n";
		return 1;
	}
	if (ResolveWireEndpoints(placedRows, 8, 5, first, second) != WireEndpointStatus::kFirstIndexOutOfRange) {
		std::cerr << "An out-of-range wire endpoint was not rejected\n";
		return 1;
	}

	std::cout << "Legacy compatibility and physical wire-row semantics accepted\n";
	return 0;
}
