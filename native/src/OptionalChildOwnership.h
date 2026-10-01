// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Clipboard::OptionalFiltering
{
	// Parent is a retaining pointer in production. Callers supply the complete
	// operation snapshot and domain-specific script/link checks; this shared
	// algorithm never infers ownership from a child's base form or location.
	template <class Parent, class Keyword, class ChildID = std::uint32_t>
	class ChildOwnershipIndex
	{
	public:
		template <class Parents, class Keywords, class IsAllowedParent, class GetChildID>
		void Build(const Parents& parents, const Keywords& keywords,
			IsAllowedParent isAllowedParent, GetChildID getChildID)
		{
			links.clear();
			for (const auto& parent : parents) {
				if (!isAllowedParent(parent)) {
					continue;
				}
				for (const auto& keyword : keywords) {
					if (const auto child = getChildID(parent, keyword)) {
						links[*child].push_back(Link{ parent, keyword });
					}
				}
			}
		}

		template <class IsCurrentLink>
		[[nodiscard]] bool Matches(ChildID child, IsCurrentLink isCurrentLink) const
		{
			const auto found = links.find(child);
			if (found != links.end()) {
				for (const auto& link : found->second) {
					if (isCurrentLink(link.parent, link.keyword)) {
						return true;
					}
				}
			}
			return false;
		}

	private:
		struct Link
		{
			Parent parent;
			Keyword keyword;
		};
		std::unordered_map<ChildID, std::vector<Link>> links;
	};
}
