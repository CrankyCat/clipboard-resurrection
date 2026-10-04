// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <RE/Bethesda/BSTSmartPointer.h>

#include <memory>

namespace Clipboard::PapyrusBinding
{
	// The VM takes a temporary intrusive reference and can release it before
	// returning false. Keep our own intrusive reference through that call so
	// rejection cannot delete the function while a unique_ptr still owns it.
	template <class VM, class Function>
	[[nodiscard]] bool Bind(VM& vm, std::unique_ptr<Function> function)
	{
		const RE::BSTSmartPointer<Function> owner{ function.release() };
		return vm.BindNativeMethod(owner.get());
	}
}
