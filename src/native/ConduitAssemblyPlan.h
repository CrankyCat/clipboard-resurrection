// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Clipboard::ConduitConnections
{
	// Functional membership, independent of logging and the capped diagnostic
	// trace. Only the game task thread accesses these containers. Never serialized.
	class AssemblyRecords
	{
	public:
		struct Pair
		{
			std::uint32_t source{}, target{}, point{};
			std::array<float, 3> position{};
			bool validationFailed{};
		};
		static constexpr std::size_t PairLimit = 262144;
		explicit AssemblyRecords(std::size_t limit = PairLimit) : _limit(limit) {}
		bool Candidate(std::uint32_t source)
		{
			if (_sealed || !source) { return Fail(); }
			_bySource.try_emplace(source);
			return true;
		}
		bool Add(std::uint32_t source, std::uint32_t target, std::uint32_t point, std::array<float, 3> position)
		{
			if (_sealed || !source || !target || source == target || !_bySource.contains(source)) { return Fail(); }
			const auto low = source < target ? source : target;
			const auto high = source < target ? target : source;
			const auto key = (static_cast<std::uint64_t>(low) << 32) | high;
			if (_keys.contains(key)) { return true; }
			if (pairs.size() >= _limit) { return Fail(); }
			_keys.insert(key);
			_bySource.at(source).push_back(pairs.size());
			pairs.push_back({ source, target, point, position, false });
			return true;
		}
		void Seal(bool success) noexcept { _complete &= success; _sealed = true; }
		bool Claim() noexcept
		{
			if (!_sealed || !_complete || _claimed) { return false; }
			_claimed = true;
			return true;
		}
		bool Ready() const noexcept { return _sealed && _complete; }
		bool Claimed() const noexcept { return _claimed; }
		bool MarkFailed(std::size_t index) noexcept
		{
			if (pairs[index].validationFailed) { return false; }
			pairs[index].validationFailed = true;
			return true;
		}
		const std::vector<std::size_t>* ForSource(std::uint32_t source) const noexcept
		{
			const auto found = _bySource.find(source);
			return found == _bySource.end() ? nullptr : &found->second;
		}
		std::vector<Pair> pairs;
	private:
		bool Fail() noexcept { _complete = false; return false; }
		std::size_t _limit;
		std::unordered_map<std::uint32_t, std::vector<std::size_t>> _bySource;
		std::unordered_set<std::uint64_t> _keys;
		bool _complete{ true }, _sealed{}, _claimed{};
	};
}
