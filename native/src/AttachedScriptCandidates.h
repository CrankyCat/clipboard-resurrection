// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace Clipboard::OptionalFiltering
{
	// The caller holds the VM attachment lock for this entire traversal. Only
	// numeric handles escape; type pointers and their memoized matches do not.
	// Names use the caller's case-insensitive pooled-name equality. Const,
	// conditional and uninitialized objects have the same membership semantics.
	template <class Attachments, class Names, class GetType, class GetName, class GetParent>
	[[nodiscard]] std::vector<std::uint64_t> CollectAttachedScriptCandidates(
		const Attachments& attachments, const Names& names,
		GetType getType, GetName getName, GetParent getParent,
		std::size_t maximumCandidates = std::numeric_limits<std::size_t>::max())
	{
		using TypePointer = decltype(getType(*attachments.begin()->second.begin()));
		std::unordered_map<TypePointer, bool> matches;
		std::vector<std::uint64_t> handles;
		for (const auto& [handle, objects] : attachments) {
			for (const auto& object : objects) {
				const auto type = getType(object);
				if (!type) {
					continue;
				}
				const auto [entry, inserted] = matches.try_emplace(type, false);
				if (inserted) {
					for (auto current = type; current; current = getParent(current)) {
						if (std::find(names.begin(), names.end(), getName(current)) != names.end()) {
							entry->second = true;
							break;
						}
					}
				}
				if (entry->second) {
					if (handles.size() == maximumCandidates) { throw std::length_error("Attached-script candidate limit exceeded"); }
					handles.push_back(handle);
					break;
				}
			}
		}
		return handles;
	}
}
