// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <RE/Bethesda/BSScript/Variable.h>

#include <memory>
#include <type_traits>

namespace Clipboard::PapyrusArgument
{
	// F4SE PackHandle stores the requested native class in VMValue::type even
	// when its identifier belongs to an attached derived script. CommonLib's
	// object assignment instead stores that derived script type. Dynamic calls
	// require the declared argument type; their validator does not upcast it.
	// See Docs/Phase4/REDUCED_FACTORY_ARGUMENT_FIX.md for executable/source proof.
	//
	// Apply only after resolving an identifier for the requested native class.
	// Variable is a standard-layout class whose first member is TypeInfo, so
	// this pointer conversion designates that member. The second member remains
	// the same owning Object smart pointer. Never change the shared Object::type.
	static_assert(std::is_standard_layout_v<RE::BSScript::Variable>);
	static_assert(sizeof(RE::BSScript::Variable) == 0x10);
	static_assert(sizeof(RE::BSScript::TypeInfo) == 0x08);
	static_assert(alignof(RE::BSScript::Variable) == alignof(RE::BSScript::TypeInfo));

	[[nodiscard]] inline bool RestoreDeclaredObjectType(
		RE::BSScript::Variable& a_value,
		const RE::BSScript::TypeInfo& a_declaredType)
	{
		if (!a_value.is<RE::BSScript::Object>() || !a_declaredType.IsObject()) {
			return false;
		}
		*reinterpret_cast<RE::BSScript::TypeInfo*>(std::addressof(a_value)) = a_declaredType;
		return true;
	}
}
