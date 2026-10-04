// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "PCH.h"

namespace Clipboard
{
	// Preserve CommonLib's engine acquisition and atomic release while resolving
	// the lock function once, rather than once for every inspected parent.
	class CachedReadLock final
	{
	public:
		explicit CachedReadLock(RE::BSReadWriteLock& lock) : lock_(lock)
		{
			using LockRead = decltype(&RE::BSReadWriteLock::lock_read);
			// Same family tuple and signature as CommonLib BSLock.h; already
			// included in RuntimeSymbols.h's reviewed dependency inventory.
			static const REL::Relocation<LockRead> lockRead{ REL::ID(1573164, 2267897) };
			lockRead(&lock_);
		}

		~CachedReadLock()
		{
			lock_.unlock_read();
		}

		CachedReadLock(const CachedReadLock&) = delete;
		CachedReadLock& operator=(const CachedReadLock&) = delete;
		CachedReadLock(CachedReadLock&&) = delete;
		CachedReadLock& operator=(CachedReadLock&&) = delete;

	private:
		RE::BSReadWriteLock& lock_;
	};

	static_assert(!std::is_copy_constructible_v<CachedReadLock>);
	static_assert(!std::is_copy_assignable_v<CachedReadLock>);
	static_assert(!std::is_move_constructible_v<CachedReadLock>);
	static_assert(!std::is_move_assignable_v<CachedReadLock>);
}
