#pragma once

#ifndef CLIPBOARD_PAPYRUS_ARRAY_ALGORITHM_ONLY
#	include "PCH.h"
#endif
#include "LegacyVMArray.h"

namespace Clipboard::PapyrusArray
{
	// Keep the row-preservation rule independently host-testable. Papyrus uses
	// kNone for an intentional null ObjectReference[] element, and resolving
	// that element as an object would make CommonLib assert and emit an error.
	template <class Result, class Range, class IsNull, class Resolve>
	[[nodiscard]] Result UnpackObjectRows(
		const Range& a_rows,
		IsNull a_isNull,
		Resolve a_resolve)
	{
		Result result;
		for (const auto& row : a_rows) {
			if (a_isNull(row)) {
				result.push_back(typename Result::value_type{});
			} else {
				result.push_back(a_resolve(row));
			}
		}
		return result;
	}
}

#ifndef CLIPBOARD_PAPYRUS_ARRAY_ALGORITHM_ONLY

namespace RE::BSScript
{
	template <class T>
	struct script_traits<::VMArray<T>> final
	{
		using is_array = std::true_type;
		using is_string = std::false_type;
		using is_nullable = std::false_type;
	};
}

// NativeFunction::DispatchHelper calls UnpackVariable unqualified. ADL brings
// BSScript's constrained array overload into that call; it wins over the
// unconstrained detail wrapper even when that wrapper has a specialization.
// Specialize the selected public overload so both dispatch and detail callers
// reach these guards. See PapyrusArrayDispatchTests for the actual SDK route.
namespace RE::BSScript
{
	template <>
	[[nodiscard]] inline ::VMArray<std::int32_t> UnpackVariable<::VMArray<std::int32_t>>(
		const RE::BSScript::Variable& variable)
	{
		::VMArray<std::int32_t> result;
		if (!variable.is<RE::BSScript::Array>()) { return result; }
		const auto array = RE::BSScript::get<RE::BSScript::Array>(variable);
		if (!array) { return result; }
		for (const auto& value : array->elements) {
			if (!value.is<std::int32_t>()) { throw std::invalid_argument("Invalid import outcome array"); }
			result.push_back(UnpackVariable<std::int32_t>(value));
		}
		return result;
	}

	// Papyrus represents None/uninitialized ObjectReference[] values as an
	// array-typed Variable with null Array storage. F4SE's historical VMArray
	// unpacker treated that value as an empty array. The pinned CommonLibF4RD
	// generic array unpacker checks the type but dereferences the null storage.
	// Specialize the incoming reference-array type used by Clipboard before any
	// NativeFunction instantiation, without modifying the pinned dependency.
	template <>
	[[nodiscard]] inline ::VMArray<RE::TESObjectREFR*> UnpackVariable<::VMArray<RE::TESObjectREFR*>>(
		const RE::BSScript::Variable& a_variable)
	{
		if (!a_variable.is<RE::BSScript::Array>()) {
			return {};
		}
		const auto array = RE::BSScript::get<RE::BSScript::Array>(a_variable);
		if (!array) {
			return {};
		}

		const auto game = RE::GameVM::GetSingleton();
		const auto vm = game ? game->GetVM() : nullptr;
		const auto* handles = vm ? std::addressof(vm->GetObjectHandlePolicy()) : nullptr;
		return Clipboard::PapyrusArray::UnpackObjectRows<::VMArray<RE::TESObjectREFR*>>(
			array->elements,
			[](const RE::BSScript::Variable& a_element) {
				if (a_element.is<std::nullptr_t>()) {
					return true;
				}
				if (!a_element.is<RE::BSScript::Object>()) {
					return false;
				}
				return !RE::BSScript::get<RE::BSScript::Object>(a_element);
			},
			[handles](const RE::BSScript::Variable& a_element) -> RE::TESObjectREFR* {
				if (!handles || !a_element.is<RE::BSScript::Object>()) {
					return nullptr;
				}

				const auto object = RE::BSScript::get<RE::BSScript::Object>(a_element);
				if (!object) {
					return nullptr;
				}

				const auto handle = object->GetHandle();
				if (!handles->IsHandleLoaded(handle)) {
					return nullptr;
				}

				return static_cast<RE::TESObjectREFR*>(handles->GetObjectForHandle(
					RE::BSScript::GetVMTypeID<RE::TESObjectREFR>(),
					handle));
			});
	}
}

#endif
