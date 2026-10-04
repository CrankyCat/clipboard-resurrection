// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include "ExistingPowerEndpoints.h"

#include <cmath>
#include <map>
#include <sstream>

namespace Clipboard::PatternExport
{
	struct Counts
	{
		std::uint32_t objects{}, plugins{}, internalWires{}, endpoints{}, externalWires{};
	};
	inline bool Number(std::string_view text, std::uint32_t& value)
	{
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
		return !text.empty() && error == std::errc{} && end == text.data() + text.size();
	}
	inline bool Finite(std::string_view text)
	{
		double value{};
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
		return !text.empty() && error == std::errc{} && end == text.data() + text.size() && std::isfinite(value);
	}

	// Validate the newly serialized snapshot, not the old file at the destination.
	// This is deliberately stricter than the legacy reader: an export may never
	// publish missing rows, invalid indices, injected sections or nonfinite values.
	inline bool Validate(std::string_view contents, const Counts& expected)
	{
		if (contents.empty() || contents.find('\0') != contents.npos || !expected.plugins || !expected.objects) { return false; }
		using Fields = std::map<std::string_view, std::string_view>;
		std::map<std::string_view, Fields> sections;
		Fields* current{};
		while (!contents.empty()) {
			const auto newline = contents.find('\n');
			const auto line = ExistingPower::Trim(contents.substr(0, newline));
			contents = newline == contents.npos ? std::string_view{} : contents.substr(newline + 1);
			if (line.empty()) { continue; }
			if (line.front() == '[' && line.back() == ']') {
				const auto [entry, inserted] = sections.emplace(line, Fields{});
				if (!inserted) { return false; }
				current = &entry->second;
			} else {
				const auto equal = line.find('=');
				if (!current || equal == line.npos || !equal ||
					!current->emplace(line.substr(0, equal), line.substr(equal + 1)).second) { return false; }
			}
		}
		const auto section = [&](std::string_view name) -> const Fields& {
			static const Fields empty;
			const auto it = sections.find(name);
			return it == sections.end() ? empty : it->second;
		};
		const auto field = [](const Fields& fields, std::string_view name) {
			const auto it = fields.find(name);
			return it == fields.end() ? std::string_view{} : it->second;
		};
		const auto number = [&](const Fields& fields, std::string_view name, std::uint32_t value) {
			std::uint32_t parsed{};
			return Number(field(fields, name), parsed) && parsed == value;
		};
		const auto index = [&](const Fields& fields, std::string_view name, std::uint32_t upper) {
			std::uint32_t parsed{};
			return Number(field(fields, name), parsed) && parsed < upper;
		};
		const auto local = [&](const Fields& fields, std::string_view name) {
			std::uint32_t parsed{};
			return Number(field(fields, name), parsed) && parsed && parsed <= 0xFFFFFF;
		};
		const auto& general = section("[general]");
		if (general.size() != 9 || !general.contains("pattern_name") || !general.contains("character") ||
			field(general, "copied_on").empty() || field(general, "clipboard_version").empty() ||
			!local(general, "workshop_id") || !index(general, "workshop_plugin", expected.plugins) ||
			!number(general, "plugin_count", expected.plugins) || !number(general, "object_count", expected.objects) ||
			!number(general, "wire_count", expected.internalWires + expected.externalWires)) { return false; }
		const auto& reference = section("[reference]");
		if (reference.size() != 8 || !local(reference, "cell_id") || !index(reference, "cell_plugin", expected.plugins)) { return false; }
		for (const auto name : { "position_x", "position_y", "position_z", "angle_x", "angle_y", "angle_z" }) {
			if (!Finite(field(reference, name))) { return false; }
		}
		const auto& plugins = section("[plugins]");
		if (plugins.size() != expected.plugins) { return false; }
		for (std::uint32_t i = 0; i < expected.plugins; ++i) {
			if (field(plugins, std::to_string(i)).empty()) { return false; }
		}
		const auto& objects = section("[objects]");
		if (objects.size() != expected.objects || !sections.contains("[objects]")) { return false; }
		for (std::uint32_t i = 0; i < expected.objects; ++i) {
			auto text = field(objects, std::to_string(i));
			for (unsigned part = 0; part < 9; ++part) {
				const auto separator = text.find('|');
				if ((part < 8) != (separator != text.npos)) { return false; }
				const auto value = text.substr(0, separator);
				std::uint32_t parsed{};
				if (part < 2) {
					if (!Number(value, parsed) || (part == 0 ? parsed >= expected.plugins : !parsed || parsed > 0xFFFFFF)) { return false; }
				} else if (!Finite(value)) { return false; }
				if (separator != text.npos) { text.remove_prefix(separator + 1); }
			}
		}
		const auto& wires = section("[wires]");
		if (wires.size() != expected.internalWires) { return false; }
		for (std::uint32_t i = 0; i < expected.internalWires; ++i) {
			const auto pair = ExistingPower::Numbers<2>(field(wires, std::to_string(i)));
			if (!pair || (*pair)[0] >= expected.objects || (*pair)[1] >= expected.objects || (*pair)[0] == (*pair)[1]) { return false; }
		}
		if (expected.externalWires) {
			const auto& metadata = section("[external_power]");
			const auto& endpoints = section("[existing_endpoints]");
			const auto& external = section("[external_wires]");
			std::uint32_t version{};
			if (metadata.size() != 1 || !Number(field(metadata, "version"), version) || (version != 1 && version != 2) || endpoints.size() != expected.endpoints ||
				external.size() != expected.externalWires) { return false; }
			for (std::uint32_t i = 0; i < expected.endpoints; ++i) {
				const auto endpoint = ExistingPower::ParseEndpoint(field(endpoints, std::to_string(i)), version);
				if (!endpoint || (!endpoint->created && endpoint->reference.plugin >= expected.plugins) || endpoint->base.plugin >= expected.plugins ||
					endpoint->cell.plugin >= expected.plugins || endpoint->workshop.plugin >= expected.plugins) { return false; }
			}
			for (std::uint32_t i = 0; i < expected.externalWires; ++i) {
				const auto wire = ExistingPower::ParseWire(field(external, std::to_string(i)));
				if (!wire || wire->objectRow >= expected.objects || wire->endpointRow >= expected.endpoints) { return false; }
			}
		} else if (expected.endpoints) { return false; }
		return sections.size() == 4 + (expected.internalWires ? 1u : 0u) + (expected.externalWires ? 3u : 0u);
	}
}
