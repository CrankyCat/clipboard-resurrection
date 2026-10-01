#pragma once

#include "PCH.h"
#include "LoggingPolicy.h"
#include "LegacyPapyrusArray.h"

using UInt8 = std::uint8_t;
using SInt8 = std::int8_t;
using UInt16 = std::uint16_t;
using SInt16 = std::int16_t;
using UInt32 = std::uint32_t;
using SInt32 = std::int32_t;
using UInt64 = std::uint64_t;
using SInt64 = std::int64_t;

using StaticFunctionTag = std::monostate;
using VirtualMachine = RE::BSScript::IVirtualMachine;
using BSFixedString = RE::BSFixedString;

using RE::Actor;
using RE::BaseFormComponent;
using RE::BGSBendableSpline;
using RE::BGSConstructibleObject;
using RE::BGSDefaultObject;
using RE::BGSKeyword;
using RE::BGSKeywordForm;
using RE::BGSListForm;
using RE::BSPointerHandle;
using RE::ExtraBendableSplineParams;
using RE::ExtraDataList;
using RE::ExtraPowerLinks;
using ModInfo = RE::TESFile;
using RE::NiPoint3;
using RE::TESForm;
using RE::TESEffectShader;
using RE::TESObjectCELL;
using RE::TESObjectREFR;
using RE::TESRace;
using RE::TESWorldSpace;

// Keep the historical implementation readable while all concrete storage and
// calls come from CommonLibF4RD's RE namespace.
using namespace RE;

inline TESForm* LookupFormByID(UInt32 a_formID)
{
	return TESForm::GetFormByID(a_formID);
}

inline constexpr auto kExtraData_LeveledCreature = EXTRA_DATA_TYPE::kLeveledCreature;
inline constexpr auto kExtraData_PowerLinks = EXTRA_DATA_TYPE::kPowerLinks;
inline constexpr auto kExtraData_BendableSplineParams = EXTRA_DATA_TYPE::kBendableSplineParams;

namespace ExtraDataType
{
	inline constexpr auto kExtraData_WorkshopExtraData = EXTRA_DATA_TYPE::kWorkshop;
}

// Read-only prefix view: the engine object is 0x88 bytes, but Clipboard only
// reads its base-form pointer at 0x18. OG 1.10.163, NG 1.10.984 and AE 1.11.240
// constructors and load paths confirm that shared field and its TESActorBase
// identity (a TESForm-derived base). See CROSS_FAMILY_ABI.md and its retained
// executable evidence. Never allocate, copy or delete an engine object using
// this partial view; live selection/template behavior remains a separate gate.
namespace RE
{
	class ExtraLeveledCreature : public BSExtraData
	{
	public:
		static constexpr auto RTTI{ RTTI::ExtraLeveledCreature };
		static constexpr auto VTABLE{ VTABLE::ExtraLeveledCreature };
		static constexpr auto TYPE{ EXTRA_DATA_TYPE::kLeveledCreature };

		TESForm* baseForm;
	};
	static_assert(sizeof(ExtraLeveledCreature) == 0x20);
}

// CommonLibF4RD exposes the RTTI IDs for this NetImmerse extra-data family but
// not its payload. The payload below matches the official F4SE 0.6.23, 0.7.2
// and 0.7.9 NiExtraData.h declarations. See Docs/Phase4/CROSS_FAMILY_ABI.md.
// Runtime certification remains separate from this source-layout comparison.
namespace RE::BSConnectPoint
{
	class Parents : public NiExtraData
	{
	public:
		static constexpr auto RTTI{ RTTI::BSConnectPoint__Parents };

		struct ConnectPoint
		{
			std::uint64_t unknown00;
			BSFixedString parent;
			BSFixedString name;
			NiQuaternion rotation;
			NiPoint3 position;
			float scale;
		};
		static_assert(sizeof(ConnectPoint) == 0x38);

		BSTArray<ConnectPoint*> points;
	};
	static_assert(sizeof(Parents) == 0x30);
}

inline RE::NiPoint3 MultiplyTranspose(const RE::NiMatrix3& a_matrix, const RE::NiPoint3& a_point)
{
	return {
		a_matrix.entry[0].pt[0] * a_point.x + a_matrix.entry[1].pt[0] * a_point.y + a_matrix.entry[2].pt[0] * a_point.z,
		a_matrix.entry[0].pt[1] * a_point.x + a_matrix.entry[1].pt[1] * a_point.y + a_matrix.entry[2].pt[1] * a_point.z,
		a_matrix.entry[0].pt[2] * a_point.x + a_matrix.entry[1].pt[2] * a_point.y + a_matrix.entry[2].pt[2] * a_point.z
	};
}

// F4SE's old RTTI macro mapped directly to the same game RTTI operation now
// exposed by TESForm::As<T>().
#define DYNAMIC_CAST(source, from, to) ((source) ? (source)->As<to>() : nullptr)

// The old member-function macro was only call syntax; CommonLibF4RD exposes
// these methods directly.
#define CALL_MEMBER_FN(object, function) ((object)->function)

template <stl::nttp::string Object, stl::nttp::string Structure>
class LegacyStructure : public RE::BSScript::structure_wrapper<Object, Structure>
{
private:
	using super = RE::BSScript::structure_wrapper<Object, Structure>;

	// The pinned CommonLibF4RD structure wrapper exposes its qualified name as
	// a string_view whose backing NTTP buffer has no guaranteed trailing NUL.
	// BSFixedString's string_view conversion requires data()[size()] to be NUL;
	// without that byte the VM can look up a different type name depending on
	// adjacent static storage. Keep an explicitly terminated tag for both
	// native-signature discovery and returned-structure construction.
	static consteval auto MakeNameStorage() noexcept
	{
		std::array<char, Object.length() + Structure.length() + 2> result{};
		for (std::size_t index = 0; index < Object.length(); ++index) {
			result[index] = Object[index];
		}
		result[Object.length()] = '#';
		for (std::size_t index = 0; index < Structure.length(); ++index) {
			result[Object.length() + 1 + index] = Structure[index];
		}
		return result;
	}

	inline static constexpr auto nameStorage = MakeNameStorage();

	[[nodiscard]] static RE::BSTSmartPointer<RE::BSScript::Struct> CreateProxy()
	{
		RE::BSTSmartPointer<RE::BSScript::Struct> proxy;
		const auto game = RE::GameVM::GetSingleton();
		const auto vm = game ? game->GetVM() : nullptr;
		if (!vm || !vm->CreateStruct(name, proxy) || !proxy) {
			F4SE::log::error("Failed to create Papyrus structure of type {}", name);
		}
		return proxy;
	}

public:
	inline static constexpr std::string_view name{ nameStorage.data(), nameStorage.size() - 1 };

	LegacyStructure() :
		super(CreateProxy())
	{}

	void SetNone(bool) noexcept {}

	template <class T>
	bool Set(std::string_view a_name, const T& a_value)
	{
		return this->insert(a_name, a_value);
	}

	template <class T>
	bool Get(std::string_view a_name, T* a_out) const
	{
		if (!a_out) {
			return false;
		}
		auto value = this->template find<T>(a_name, true);
		if (!value) {
			// F4SE's VMStruct::Get leaves the destination unchanged when the
			// requested member is absent.  Several inherited callers supply a
			// deliberate fallback value before probing an optional member.
			return false;
		}
		*a_out = std::move(*value);
		return true;
	}

protected:
	friend struct RE::BSScript::detail::wrapper_accessor;
	explicit LegacyStructure(RE::BSTSmartPointer<RE::BSScript::Struct> a_proxy) noexcept :
		super(std::move(a_proxy))
	{}
};

namespace RE::BSScript::detail
{
	template <stl::nttp::string Object, stl::nttp::string Structure>
	struct _is_structure_wrapper<::LegacyStructure<Object, Structure>> : std::true_type
	{};
}

using SelectionDetails = LegacyStructure<"ClipboardExtension", "SelectionDetails">;
using PatternObjectEntry = LegacyStructure<"ClipboardExtension", "PatternObjectEntry">;
using PatternWireEntry = LegacyStructure<"ClipboardExtension", "PatternWireEntry">;
using PatternGeneralEntry = LegacyStructure<"ClipboardExtension", "PatternGeneralEntry">;
using PatternReferenceEntry = LegacyStructure<"ClipboardExtension", "PatternReferenceEntry">;
using ComponentEntry = LegacyStructure<"ClipboardExtension", "ComponentEntry">;
using ImportPlacementRow = LegacyStructure<"ClipboardExtension", "ImportPlacementRow">;

namespace RE::BSScript::detail
{
	// Incoming uninitialized struct arrays must not dereference null VM storage.
	template <>
	[[nodiscard]] inline ::VMArray<::ImportPlacementRow> UnpackVariable<::VMArray<::ImportPlacementRow>>(
		const RE::BSScript::Variable& variable)
	{
		::VMArray<::ImportPlacementRow> result;
		if (!variable.is<RE::BSScript::Array>()) { return result; }
		const auto array = RE::BSScript::get<RE::BSScript::Array>(variable);
		if (!array) { return result; }
		for (const auto& value : array->elements) {
			if (!value.is<RE::BSScript::Struct>() || !RE::BSScript::get<RE::BSScript::Struct>(value)) {
				throw std::invalid_argument("Invalid import placement row");
			}
			result.push_back(wrapper_accessor::construct<::ImportPlacementRow>(value));
		}
		return result;
	}
}

static_assert(SelectionDetails::name.data()[SelectionDetails::name.size()] == '\0');
static_assert(PatternObjectEntry::name.data()[PatternObjectEntry::name.size()] == '\0');
static_assert(PatternWireEntry::name.data()[PatternWireEntry::name.size()] == '\0');
static_assert(PatternGeneralEntry::name.data()[PatternGeneralEntry::name.size()] == '\0');
static_assert(PatternReferenceEntry::name.data()[PatternReferenceEntry::name.size()] == '\0');
static_assert(ComponentEntry::name.data()[ComponentEntry::name.size()] == '\0');
static_assert(ImportPlacementRow::name.data()[ImportPlacementRow::name.size()] == '\0');

#define _MESSAGE(...) CLIPBOARD_DEBUG_LOG(logger::info(__VA_ARGS__))
