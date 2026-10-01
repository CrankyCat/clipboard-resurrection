// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <exception>
#include <functional>
#include <utility>

namespace Clipboard::NativeCallBoundary
{
	// Contain C++ failures in argument unpacking, the native implementation and
	// return packing. A rejected dispatch remains a failure; never substitute a
	// successful default result. This is not an access-violation/SEH handler.
	template <class Dispatch, class Failure>
	[[nodiscard]] bool Invoke(Dispatch&& dispatch, Failure&& failure) noexcept
	{
		try {
			return std::invoke(std::forward<Dispatch>(dispatch));
		} catch (const std::exception& error) {
			try { std::invoke(failure, error.what()); } catch (...) {}
		} catch (...) {
			try { std::invoke(failure, "unknown C++ exception"); } catch (...) {}
		}
		return false;
	}
}
