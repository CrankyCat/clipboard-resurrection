// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "LegacyPapyrusArray.h"

namespace RE::BSScript::detail
{
	// Match DispatchHelper's unqualified dependent call, including ADL. Testing
	// only a qualified detail::UnpackVariable call misses its overload choice.
	template <class T>
	[[nodiscard]] T ClipboardTestArgumentDispatch(const Variable& value)
	{
		return UnpackVariable<decay_t<T>>(value);
	}
}

namespace Clipboard::PapyrusArrayDispatchTests
{
	using Variable = RE::BSScript::Variable;
	using TypeInfo = RE::BSScript::TypeInfo;
	using References = VMArray<RE::TESObjectREFR*>;
	using Integers = VMArray<std::int32_t>;

	inline void SetType(Variable& value, TypeInfo::RawType type)
	{
		// The standard-layout Variable's first member is TypeInfo, as also used
		// by PapyrusArgument. Its default-constructed null payload stays intact.
		static_assert(std::is_standard_layout_v<Variable> && sizeof(Variable) == 0x10);
		static_assert(sizeof(TypeInfo) == 0x08 && alignof(Variable) == alignof(TypeInfo));
		*reinterpret_cast<TypeInfo*>(std::addressof(value)) = type;
	}

	inline bool DispatchNullArrays()
	{
		Variable references, integers;
		SetType(references, TypeInfo::RawType::kArrayObject);
		SetType(integers, TypeInfo::RawType::kArrayInt);
		return RE::BSScript::detail::ClipboardTestArgumentDispatch<References>(references).empty() &&
			RE::BSScript::detail::ClipboardTestArgumentDispatch<Integers>(integers).empty();
	}

	inline bool PublicNullArrays()
	{
		Variable references, integers;
		SetType(references, TypeInfo::RawType::kArrayObject);
		SetType(integers, TypeInfo::RawType::kArrayInt);
		return RE::BSScript::UnpackVariable<References>(references).empty() &&
			RE::BSScript::UnpackVariable<Integers>(integers).empty();
	}

	// Restrict SEH to the null-storage regression probe. A wrong overload
	// dereferences null before any game call; report a failed test without WER.
	inline bool GuardNullStorageProbe(bool (*probe)())
	{
		__try { return probe(); }
		__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
			return false;
		}
	}

	template <class Require>
	void Run(Require&& require)
	{
		require(GuardNullStorageProbe(DispatchNullArrays), "Native argument ADL dispatch must protect typed-null arrays");
		require(GuardNullStorageProbe(PublicNullArrays), "Public CommonLib array unpacking must use the same null guard");
		Variable none;
		require(RE::BSScript::detail::ClipboardTestArgumentDispatch<References>(none).empty(), "None reference array must stay empty");
		require(RE::BSScript::detail::ClipboardTestArgumentDispatch<Integers>(none).empty(), "None outcome array must stay empty");
	}
}
