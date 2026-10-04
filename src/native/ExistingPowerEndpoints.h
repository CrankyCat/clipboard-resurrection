// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <array>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace Clipboard::ExistingPower
{
	inline constexpr std::size_t kMaximumEndpoints = 1024;
	inline constexpr std::size_t kMaximumWires = 4096;
	struct Form { std::uint32_t plugin{}, local{}; };
	struct Endpoint
	{
		Form reference, base, cell, workshop;
		// Created references use their save-local FF FormID, never a plugin lookup.
		bool created{};
		std::array<double, 3> position{};
	};
	struct Wire { std::uint32_t objectRow{}, endpointRow{}; };
	struct Bundle
	{
		bool present{}, valid{ true };
		std::uint32_t version{};
		std::string error;
		// Invalid records retain holes so later physical indices never shift.
		std::vector<std::optional<Endpoint>> endpoints;
		std::vector<std::optional<Wire>> wires;
	};
	inline std::string_view Trim(std::string_view text)
	{
		const auto first = text.find_first_not_of(" \t\r\n");
		if (first == text.npos) { return {}; }
		return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
	}
	inline bool Equal(std::string_view a, std::string_view b)
	{
		if (a.size() != b.size()) { return false; }
		for (std::size_t i = 0; i < a.size(); ++i) {
			const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
			if (lower(a[i]) != lower(b[i])) { return false; }
		}
		return true;
	}
	template <std::size_t N>
	std::optional<std::array<std::uint32_t, N>> Numbers(std::string_view text)
	{
		std::array<std::uint32_t, N> values{};
		for (std::size_t i = 0; i < N; ++i) {
			const auto end = text.find('|');
			if ((i + 1 < N) != (end != text.npos)) { return {}; }
			const auto part = Trim(text.substr(0, end));
			if (part.empty()) { return {}; }
			const auto [ptr, error] = std::from_chars(part.data(), part.data() + part.size(), values[i]);
			if (error != std::errc{} || ptr != part.data() + part.size()) { return {}; }
			if (end != text.npos) { text.remove_prefix(end + 1); }
		}
		return values;
	}
	inline bool SamePosition(const std::array<double, 3>& a, const std::array<double, 3>& b)
	{
		// Only serialization/float rounding slack, not a placement search radius.
		for (std::size_t i = 0; i < a.size(); ++i) {
			if (!std::isfinite(a[i]) || !std::isfinite(b[i]) || std::abs(a[i] - b[i]) > 0.01) { return false; }
		}
		return true;
	}
	inline std::optional<Endpoint> ParseEndpoint(std::string_view text, std::uint32_t version = 1)
	{
		if (version == 2) {
			std::array<std::string_view, 12> fields{};
			for (std::size_t i = 0; i < fields.size(); ++i) {
				const auto end = text.find('|');
				if ((i + 1 < fields.size()) != (end != text.npos)) { return {}; }
				fields[i] = Trim(text.substr(0, end));
				if (end != text.npos) { text.remove_prefix(end + 1); }
			}
			std::array<std::uint32_t, 9> values{};
			for (std::size_t i = 0; i < values.size(); ++i) {
				const auto part = fields[i];
				const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), values[i]);
				if (part.empty() || error != std::errc{} || end != part.data() + part.size()) { return {}; }
			}
			if (values[8] > 1 || (values[8] ? values[0] != 0 || (values[1] >> 24) != 0xFF || !(values[1] & 0xFFFFFF) :
				!values[1] || values[1] > 0xFFFFFF)) { return {}; }
			for (std::size_t i : { 3u, 5u, 7u }) { if (!values[i] || values[i] > 0xFFFFFF) { return {}; } }
			Endpoint result{ {values[0], values[1]}, {values[2], values[3]}, {values[4], values[5]},
				{values[6], values[7]}, values[8] != 0 };
			for (std::size_t i = 0; i < 3; ++i) {
				const auto part = fields[i + 9];
				const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), result.position[i]);
				if (part.empty() || error != std::errc{} || end != part.data() + part.size() || !std::isfinite(result.position[i])) { return {}; }
			}
			return result;
		}
		if (version != 1) { return {}; }
		const auto fields = Numbers<8>(text);
		if (!fields) { return {}; }
		const auto& f = *fields;
		for (std::size_t i = 1; i < f.size(); i += 2) {
			if (!f[i] || f[i] > 0xFFFFFF) { return {}; }
		}
		return Endpoint{ { f[0], f[1] }, { f[2], f[3] }, { f[4], f[5] }, { f[6], f[7] } };
	}
	inline std::optional<Wire> ParseWire(std::string_view text)
	{
		const auto fields = Numbers<2>(text);
		if (!fields) { return {}; }
		return Wire{ (*fields)[0], (*fields)[1] };
	}
	inline bool LocalIdFits(std::uint32_t local, bool light)
	{
		return local && local <= (light ? 0xFFFu : 0xFFFFFFu);
	}
	inline std::optional<std::uint32_t> OtherEndpoint(std::uint32_t selected, std::uint32_t a, std::uint32_t b)
	{
		if (a == b) { return {}; }
		if (selected == a) { return b; }
		if (selected == b) { return a; }
		return {};
	}
	inline void Write(std::ostream& file, const std::vector<Endpoint>& endpoints, const std::vector<Wire>& wires)
	{
		if (wires.empty()) { return; }
		const auto precision = file.precision();
		file << std::setprecision(std::numeric_limits<double>::max_digits10);
		file << "[external_power]\nversion=2\n[existing_endpoints]\n";
		for (std::size_t i = 0; i < endpoints.size(); ++i) {
			const auto& e = endpoints[i];
			file << i << '=' << e.reference.plugin << '|' << e.reference.local << '|'
				<< e.base.plugin << '|' << e.base.local << '|' << e.cell.plugin << '|' << e.cell.local << '|'
				<< e.workshop.plugin << '|' << e.workshop.local << '|' << (e.created ? 1 : 0) << '|'
				<< e.position[0] << '|' << e.position[1] << '|' << e.position[2] << '\n';
		}
		file << "[external_wires]\n";
		for (std::size_t i = 0; i < wires.size(); ++i) {
			file << i << '=' << wires[i].objectRow << '|' << wires[i].endpointRow << '\n';
		}
		file.precision(precision);
	}
	// New sections are additive. Legacy object/plugin/wire rows are untouched.
	// Reject duplicate sections and unknown versions before creating any external wire.
	inline Bundle Read(std::istream& input)
	{
		Bundle result;
		enum Section { Other, Metadata, Endpoints, Wires } section = Other;
		std::array<bool, 4> seen{};
		bool version{};
		std::vector<std::string> endpointRecords;
		std::string line;
		auto fail = [&](const char* reason) { result.valid = false; result.error = reason; };
		while (std::getline(input, line)) {
			const auto text = Trim(line);
			if (text.empty() || text.front() == ';' || text.front() == '#') { continue; }
			if (text.front() == '[' && text.back() == ']') {
				section = Equal(text, "[external_power]") ? Metadata :
					Equal(text, "[existing_endpoints]") ? Endpoints : Equal(text, "[external_wires]") ? Wires : Other;
				if (section == Other) { continue; }
				result.present = true;
				if (seen[section]) { fail("duplicate external section"); return result; }
				seen[section] = true;
				continue;
			}
			if (section == Other) { continue; }
			if (text.size() > 512) { fail("external record too long"); return result; }
			const auto separator = text.find('=');
			const auto value = separator == text.npos ? std::string_view{} : Trim(text.substr(separator + 1));
			if (section == Metadata) {
				if (separator == text.npos || !Equal(Trim(text.substr(0, separator)), "version") || version || (value != "1" && value != "2")) {
					fail("unsupported or malformed external version"); return result;
				}
				version = true;
				result.version = value == "2" ? 2u : 1u;
			} else if (section == Endpoints) {
				if (endpointRecords.size() == kMaximumEndpoints) { fail("external endpoint limit exceeded"); return result; }
				endpointRecords.emplace_back(value);
			} else {
				if (result.wires.size() == kMaximumWires) { fail("external wire limit exceeded"); return result; }
				result.wires.push_back(ParseWire(value));
			}
		}
		if (input.bad()) { fail("external section read failed"); }
		if (result.present && (!version || !seen[Endpoints] || !seen[Wires])) { fail("incomplete external sections"); }
		if (result.valid) {
			for (const auto& record : endpointRecords) { result.endpoints.push_back(ParseEndpoint(record, result.version)); }
		}
		return result;
	}

	struct Eligibility
	{
		bool stablePersistent{}, live{}, baseMatches{}, cellMatches{}, sameWorkshop{},
			insideWorkshop{}, ownerAllowed{}, allowedByPolicy{}, loaded{};
	};
	inline const char* Reject(const Eligibility& value)
	{
		if (!value.live) { return "reference missing, deleted or disabled"; }
		if (!value.stablePersistent) { return "reference identity or recorded position does not match"; }
		if (!value.baseMatches) { return "reference base does not match"; }
		if (!value.cellMatches) { return "reference cell does not match"; }
		if (!value.sameWorkshop) { return "destination workshop differs from the recorded source"; }
		if (!value.insideWorkshop) { return "endpoint is outside the destination workshop"; }
		if (!value.ownerAllowed) { return "endpoint belongs to another workshop"; }
		if (!value.allowedByPolicy) { return "endpoint is excluded by policy"; }
		if (!value.loaded) { return "endpoint 3D is not loaded"; }
		return nullptr;
	}
}
