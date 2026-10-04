// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ExistingPowerEndpoints.h"
#include <sstream>

template <class Check>
void CheckExistingPowerEndpoints(Check&& check)
{
	using namespace Clipboard::ExistingPower;
	const auto read = [](const std::string& text) { std::istringstream input(text); return Read(input); };
	const std::string legacy = "[general]\nobject_count=2\nwire_count=1\n[plugins]\n0=Fallout4.esm\n"
		"[objects]\n0=0|1|1|0|0|0|0|0|0\n1=0|2|1|0|0|0|0|0|0\n[wires]\n0=0|1\n";
	auto result = read(legacy);
	check(result.valid && !result.present && result.endpoints.empty() && result.wires.empty(),
		"legacy pattern has no external power work and retains ordinary wire interpretation");
	std::ostringstream output;
	Write(output, {}, {});
	check(output.str().empty(), "patterns without external wires do not acquire new sections");
	const Endpoint generator{ { 1, 0x1338 }, { 1, 0x42B9 }, { 1, 0xF31 }, { 1, 0xF30 } };
	Write(output, { generator }, { { 1, 0 } });
	const auto extra = output.str();
	result = read(legacy + extra);
	check(result.valid && result.present && result.endpoints.size() == 1 && result.wires.size() == 1 &&
		result.endpoints[0] && result.endpoints[0]->reference.local == 0x1338 &&
		result.endpoints[0]->base.local == 0x42B9 && result.wires[0] && result.wires[0]->objectRow == 1,
		"connector and lamp round trip carries one generator identity and external wire independently of internal wire");
	check(extra.find("1|4920|1|17081") != std::string::npos, "writer stores decimal plugin-local identities");
	result = read("[EXTERNAL_WIRES]\n9=1|0\n[EXISTING_ENDPOINTS]\n7=1|4920|1|17081|1|3889|1|3888\n"
		"[EXTERNAL_POWER]\n VERSION = 1 \n");
	check(result.valid && result.wires.size() == 1 && result.wires[0]->endpointRow == 0,
		"external sections accept arbitrary order and key spelling while using physical row order");
	result = read("[external_power]\nversion=1\n[existing_endpoints]\n0=bad\n1=1|4920|1|17081|1|3889|1|3888\n"
		"[external_wires]\n0=bad\n1=1|1\n");
	check(result.valid && result.endpoints.size() == 2 && !result.endpoints[0] && result.endpoints[1] &&
		result.wires.size() == 2 && !result.wires[0] && result.wires[1]->endpointRow == 1,
		"malformed rows retain holes instead of rewiring later references to shifted endpoints");
	for (const auto* invalid : { "1|2|3", "-1|0", "1|", "1|0junk", "4294967296|0", "|0" }) {
		check(!ParseWire(invalid), "external wire rejects malformed and overflowing indices");
	}
	for (const auto* invalid : { "0|0|0|1|0|1|0|1", "0|16777216|0|1|0|1|0|1", "0|1|0|1", "0|1|0|1|0|1|0|1|9" }) {
		check(!ParseEndpoint(invalid), "external endpoint requires all four nonzero bounded identities");
	}
	check(LocalIdFits(0xFFFFFF, false) && !LocalIdFits(0x1000000, false) &&
		LocalIdFits(0xFFF, true) && !LocalIdFits(0x1000, true) && !LocalIdFits(0, false),
		"full and light plugin limits cannot spill into another runtime identity");
	check(OtherEndpoint(4, 4, 8) == 8u && OtherEndpoint(4, 8, 4) == 8u &&
		!OtherEndpoint(4, 8, 9) && !OtherEndpoint(4, 4, 4),
		"export discovers either wire orientation but rejects stale adjacency and self connections");
	for (const auto& bad : {
		extra + "[external_wires]\n0=0|0\n",
		extra + "[existing_endpoints]\n",
		extra + "[external_power]\nversion=1\n",
		std::string("[external_power]\nversion=3\n[existing_endpoints]\n[external_wires]\n"),
		std::string("[external_power]\nversion=1\nversion=1\n[existing_endpoints]\n[external_wires]\n"),
		std::string("[external_power]\nversion=1\n[existing_endpoints]\n"),
		std::string("[existing_endpoints]\n[external_wires]\n"),
		std::string("[external_power]\nversion=1\n[existing_endpoints]\n") + std::string(513, 'x') }) {
		result = read(bad);
		check(result.present && !result.valid && !result.error.empty(), "ambiguous or unsupported external format fails before wiring");
	}
	std::string many = "[external_power]\nversion=1\n[existing_endpoints]\n";
	for (std::size_t i = 0; i < kMaximumEndpoints; ++i) { many += "0=1|4920|1|17081|1|3889|1|3888\n"; }
	check(read(many + "[external_wires]\n").valid, "endpoint record cap accepts exact boundary");
	check(!read(many + "0=bad\n[external_wires]\n").valid, "endpoint cap includes malformed records");
	many = "[external_power]\nversion=1\n[existing_endpoints]\n[external_wires]\n";
	for (std::size_t i = 0; i < kMaximumWires; ++i) { many += "0=0|0\n"; }
	check(read(many).valid && !read(many + "0=0|0\n").valid, "external wire cap is enforced");
	// v2 keeps save-local references separate from plugin-local resolution.
	Endpoint created{ {0, 0xFF123456}, {1, 17081}, {1, 3889}, {1, 3888}, true, {-1523.125, 2.5, 12000.25} };
	std::ostringstream mixed;
	Write(mixed, {generator, created}, {{0, 0}, {1, 1}});
	result = read(mixed.str());
	check(result.valid && result.version == 2 && result.endpoints.size() == 2 && !result.endpoints[0]->created &&
		result.endpoints[1]->created && result.endpoints[1]->reference.local == 0xFF123456 &&
		result.endpoints[1]->position == created.position, "mixed preplaced and player-created endpoints retain exact identities and positions");
	result = read("[existing_endpoints]\n0=0|4279383126|1|17081|1|3889|1|3888|1|-1523.125|2.5|12000.25\n"
		"[external_wires]\n0=0|0\n[external_power]\nversion=2\n");
	check(result.valid && result.endpoints[0] && result.endpoints[0]->created, "v2 parsing remains independent of section order");
	for (const auto* record : { "0|1234|1|1|1|1|1|1|1|0|0|0", "1|4279383126|1|1|1|1|1|1|1|0|0|0",
		"0|4279383126|1|1|1|1|1|1|0|0|0|0", "0|4279383126|1|1|1|1|1|1|2|0|0|0",
		"0|4279383126|1|1|1|1|1|1|1|nan|0|0", "0|4279383126|1|1|1|1|1|1|1|0|inf|0",
		"0|4279383126|1|1|1|1|1|1|1|0|0", "0|4279383126|1|1|1|1|1|1|1|0|0|0|9" }) {
		check(!ParseEndpoint(record, 2), "v2 rejects ambiguous reference kinds and malformed/nonfinite positions");
	}
	check(SamePosition({100, 200, -300}, {100.001, 200, -300}), "position guard permits only rounding slack");
	for (const auto& moved : { std::array<double,3>{101, 200, -300}, {100, 201, -300}, {100, 200, -301},
		{std::numeric_limits<double>::quiet_NaN(), 200, -300} }) {
		check(!SamePosition({100, 200, -300}, moved), "position guard rejects movement on every axis and unknown positions");
	}
	Eligibility allowed{ true, true, true, true, true, true, true, true, true };
	check(!Reject(allowed), "validated loaded existing endpoint is usable without placement ownership");
	for (auto gate : { &Eligibility::stablePersistent, &Eligibility::live, &Eligibility::baseMatches,
		&Eligibility::cellMatches, &Eligibility::sameWorkshop, &Eligibility::insideWorkshop,
		&Eligibility::ownerAllowed, &Eligibility::allowedByPolicy, &Eligibility::loaded }) {
		auto rejected = allowed; rejected.*gate = false;
		check(Reject(rejected) != nullptr, "every identity, destination, lifecycle and policy gate independently rejects unsafe external wiring");
	}
}
