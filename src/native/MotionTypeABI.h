// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace RE::BSScript
{
	class IVirtualMachine;
	class Variable;
}

namespace Clipboard::EngineAPI::MotionTypeABI
{
	// ABI input to the synchronous SetMotionTypeFunctor execution body, not an
	// engine-owned/queued functor. The reviewed body never reads or dispatches
	// through the first 0x0C bytes, retains this storage, or changes its lifetime.
	// See .local/codex/docs/Phase4/IMPORT_MOTION_PRIMITIVE.md for all four executable traces.
	struct CallData
	{
		std::array<std::byte, 0x0C> unusedHeader{};
		std::uint32_t stackID{};                 // 0C
		std::uint32_t referenceHandle{};         // 10
		std::int32_t motionType{ 2 };            // 14: ObjectReference.Motion_Keyframed
		RE::BSScript::IVirtualMachine* vm{};     // 18
		bool allowActivate{ true };             // 20
	};
	static_assert(std::is_standard_layout_v<CallData>);
	static_assert(std::is_trivially_destructible_v<CallData>);
	static_assert(sizeof(CallData) == 0x28);
	static_assert(alignof(CallData) == 0x08);
	static_assert(offsetof(CallData, stackID) == 0x0C);
	static_assert(offsetof(CallData, referenceHandle) == 0x10);
	static_assert(offsetof(CallData, motionType) == 0x14);
	static_assert(offsetof(CallData, vm) == 0x18);
	static_assert(offsetof(CallData, allowActivate) == 0x20);

	// Windows x64 member return ABI: RCX is call data, RDX points at caller-owned
	// 16-byte Variable return storage. The engine initializes None and returns
	// that same storage address in RAX. No Papyrus stack resume occurs here.
	using Execute_t = RE::BSScript::Variable* (*)(const CallData*, RE::BSScript::Variable*);
}
