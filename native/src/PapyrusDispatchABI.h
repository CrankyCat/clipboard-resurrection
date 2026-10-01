// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace Clipboard::PapyrusDispatchABI
{
	inline constexpr std::size_t kOGCallableOffset = 0x18;
	inline constexpr std::size_t kModernCallableOffset = 0x38;

	struct OGFunctionView
	{
		std::byte storage[kOGCallableOffset]{};
		void* callable{};
	};
	struct ModernFunctionView
	{
		std::byte storage[kModernCallableOffset]{};
		void* callable{};
	};
	static_assert(std::is_standard_layout_v<OGFunctionView>);
	static_assert(std::is_standard_layout_v<ModernFunctionView>);
	static_assert(offsetof(OGFunctionView, callable) == kOGCallableOffset);
	static_assert(offsetof(ModernFunctionView, callable) == kModernCallableOffset);
	static_assert(sizeof(OGFunctionView) == 0x20);
	static_assert(sizeof(ModernFunctionView) == 0x40);

	// MSVC's owning std::function has a 56-byte inline buffer and its callable
	// pointer at +0x38. The game consumes only that callable synchronously, at
	// +0x18 on OG and +0x38 on NG/AE. Its virtual invoke slot is +0x10 in all
	// three fixtures. The views never own, copy or destroy the callable.
	// See Docs/Phase4/WORKSHOP_CALLBACK_COMPLETION.md for exact binary evidence.
	static_assert(std::_Space_size == kModernCallableOffset);

	template <class Signature>
	class BorrowedFunction
	{
	public:
		using Function = std::function<Signature>;
		static_assert(sizeof(Function) == sizeof(ModernFunctionView));
		static_assert(alignof(Function) == alignof(ModernFunctionView));

		explicit BorrowedFunction(Function a_function) :
			function_(std::move(a_function))
		{
			std::memcpy(std::addressof(modern_.callable),
				reinterpret_cast<const std::byte*>(std::addressof(function_)) + kModernCallableOffset,
				sizeof(modern_.callable));
			original_.callable = modern_.callable;
		}

		BorrowedFunction(const BorrowedFunction&) = delete;
		BorrowedFunction(BorrowedFunction&&) = delete;
		BorrowedFunction& operator=(const BorrowedFunction&) = delete;
		BorrowedFunction& operator=(BorrowedFunction&&) = delete;

		[[nodiscard]] const void* View(bool a_originalGeneration) const noexcept
		{
			return a_originalGeneration ? static_cast<const void*>(std::addressof(original_)) :
				static_cast<const void*>(std::addressof(modern_));
		}

	private:
		Function function_;
		OGFunctionView original_;
		ModernFunctionView modern_;
	};
}
