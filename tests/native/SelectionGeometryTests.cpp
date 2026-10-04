// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#include "SelectionGeometry.h"
#include "ScaleConstraintsTests.h"
#include "LegacyVMArray.h"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
	using Clipboard::SelectionGeometry::Geometry;
	using Clipboard::SelectionGeometry::Point;
	struct Object
	{
		Point position;
		float scale;
	};

	// Independent copy of the pre-optimization arithmetic, including null-row
	// averaging, first-row initialization, branch order, and float summation.
	Geometry Legacy(const VMArray<Object*>& objects)
	{
		Geometry result;
		if (objects.empty()) { return result; }
		result.minimum = objects[0]->position;
		result.maximum = result.minimum;
		result.minimumScale = objects[0]->scale;
		result.maximumScale = result.minimumScale;
		float total = result.minimumScale;
		for (std::uint32_t i = 1; i < objects.Length(); ++i) {
			const auto* object = objects[i];
			if (object) {
				const float scale = object->scale;
				if (object->position.x < result.minimum.x) { result.minimum.x = object->position.x; }
				else if (object->position.x > result.maximum.x) { result.maximum.x = object->position.x; }
				if (object->position.y < result.minimum.y) { result.minimum.y = object->position.y; }
				else if (object->position.y > result.maximum.y) { result.maximum.y = object->position.y; }
				if (object->position.z < result.minimum.z) { result.minimum.z = object->position.z; }
				else if (object->position.z > result.maximum.z) { result.maximum.z = object->position.z; }
				if (scale < result.minimumScale) { result.minimumScale = scale; }
				else if (scale > result.maximumScale) { result.maximumScale = scale; }
				total += scale;
			}
		}
		result.averageScale = total / static_cast<float>(objects.Length());
		return result;
	}

	float LegacyFactor(const Geometry& geometry, float requested)
	{
		float finalScale = requested;
		if (requested <= 0.0F) {
			finalScale = geometry.averageScale > 0.0F ? 1.0F / geometry.averageScale : 1.0F;
		}
		if (!std::isfinite(finalScale) || finalScale <= 0.0F) { finalScale = 1.0F; }
		if (geometry.minimumScale > 0.0F && geometry.minimumScale * finalScale < 0.01F) {
			finalScale = 0.01F / geometry.minimumScale;
		}
		if (geometry.maximumScale > 0.0F && geometry.maximumScale * finalScale > 10.0F) {
			finalScale = 10.0F / geometry.maximumScale;
		}
		return finalScale;
	}

	bool Same(float left, float right)
	{
		return (std::isnan(left) && std::isnan(right)) ||
			std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
	}
	bool Same(Point left, Point right)
	{
		return Same(left.x, right.x) && Same(left.y, right.y) && Same(left.z, right.z);
	}

	// Compare center/area with the original operations rather than invoking the
	// production accessors on both sides and accidentally testing them against themselves.
	Point LegacyCenter(const Geometry& geometry)
	{
		return {
			static_cast<float>((geometry.minimum.x + geometry.maximum.x) / 2.0),
			static_cast<float>((geometry.minimum.y + geometry.maximum.y) / 2.0),
			static_cast<float>((geometry.minimum.z + geometry.maximum.z) / 2.0)
		};
	}
	Point LegacyArea(const Geometry& geometry)
	{
		return { geometry.maximum.x - geometry.minimum.x,
			geometry.maximum.y - geometry.minimum.y, geometry.maximum.z - geometry.minimum.z };
	}
}

int main()
{
	int checks{}, failures{};
	const auto check = [&](bool passed, const char* name) {
		++checks;
		if (!passed) { ++failures; std::cerr << name << '\n'; }
	};
	const auto readPosition = [](const Object* object) { return object->position; };
	const auto readScale = [](const Object* object) { return object->scale; };
	const auto verify = [&](const VMArray<Object*>& objects) {
		const auto expected = Legacy(objects);
		const auto actual = Clipboard::SelectionGeometry::Measure(objects, readPosition, readScale);
		const auto centerOnly = Clipboard::SelectionGeometry::Measure<false>(objects, readPosition, readScale);
		check(Same(actual.minimum, expected.minimum), "minimum coordinates match original");
		check(Same(actual.maximum, expected.maximum), "maximum coordinates match original");
		check(Same(actual.Center(), LegacyCenter(expected)), "midpoint matches original arithmetic");
		check(Same(actual.Area(), LegacyArea(expected)), "area matches original arithmetic");
		check(Same(actual.minimumScale, expected.minimumScale), "minimum scale matches original");
		check(Same(actual.maximumScale, expected.maximumScale), "maximum scale matches original");
		check(Same(actual.averageScale, expected.averageScale), "average scale matches original order and divisor");
		check(Same(centerOnly.Center(), LegacyCenter(expected)), "center-only query retains bounds midpoint");
		const std::array requested{ 0.001F, 0.5F, 1.0F, 1.1F, 10000.0F,
			std::numeric_limits<float>::infinity(),
			std::numeric_limits<float>::quiet_NaN() };
		for (const auto factor : requested) {
			for (const bool whole : { false, true }) {
				const auto plan = Clipboard::SelectionGeometry::PlanScale(actual, factor, whole);
				check(!plan.restore && plan.movePositions == whole && Same(plan.factor, LegacyFactor(expected, factor)),
					"relative scaling preserves factor, fallback, clamps and position policy");
				bool objectScalesMatch = true;
				for (const auto* object : objects) {
					if (object) {
						objectScalesMatch &= Same(plan.ObjectScale(object->scale), object->scale * LegacyFactor(expected, factor));
					}
				}
				check(objectScalesMatch, "relative scaling retains every object's existing scale ratio");
			}
		}
	};

	verify({});
	// Restore is absolute even when the average is already one. Mixed groups
	// have no single inverse spacing factor, so both menu choices leave positions.
	for (const std::vector<float> scales : { std::vector{ 0.5F, 1.0F, 1.0F },
		std::vector{ 0.5F, 1.0F, 1.5F }, std::vector{ 0.5F, 2.0F },
		std::vector{ 0.01F, 10.0F }, std::vector{ 1.0F, 0.8F, 1.0F, 1.3F, 1.3F, 0.7F } }) {
		std::vector<Object> fixture;
		for (const float value : scales) { fixture.push_back({ { value * 7.0F, -value, 43.0F }, value }); }
		VMArray<Object*> rows;
		for (auto& object : fixture) { rows.push_back(&object); }
		const auto geometry = Clipboard::SelectionGeometry::Measure(rows, readPosition, readScale);
		for (const bool whole : { false, true }) {
			for (const float sentinel : { -1.0F, 0.0F, -0.0F, -std::numeric_limits<float>::infinity() }) {
				const auto plan = Clipboard::SelectionGeometry::PlanScale(geometry, sentinel, whole);
				check(plan.restore && !plan.movePositions && plan.factor == 1.0F,
					"mixed restore leaves positions unchanged for either menu and any restore sentinel");
				for (const auto& object : fixture) {
					check(plan.ObjectScale(object.scale) == 1.0F, "every mixed-scale object restores to exactly 100 percent");
				}
			}
		}
	}
	for (const float scale : { 0.01F, 0.5F, 0.8F, 1.0F, 1.3F, 2.0F, 10.0F }) {
		Object left{ { -20.0F, 10.0F, 70.0F }, scale }, right{ { 20.0F, -10.0F, 170.0F }, scale };
		const auto geometry = Clipboard::SelectionGeometry::Measure(VMArray<Object*>{ &left, &right }, readPosition, readScale);
		for (const bool whole : { false, true }) {
			const auto plan = Clipboard::SelectionGeometry::PlanScale(geometry, -1.0F, whole);
			check(plan.restore && plan.ObjectScale(left.scale) == 1.0F && plan.ObjectScale(right.scale) == 1.0F,
				"uniform restore assigns exactly one rather than multiplying a rounded reciprocal");
			check(plan.movePositions == (whole && scale != 1.0F) && plan.factor == (whole ? 1.0F / scale : 1.0F),
				"only uniform Whole Restore reverses spacing; 100 percent never moves");
		}
	}
	// A null later row must not change the inverse factor through the historical
	// details average divisor. Only actual minimum/maximum scales classify a group.
	Object uniform{ { 10.0F, 20.0F, 30.0F }, 0.5F };
	const auto uniformWithNull = Clipboard::SelectionGeometry::Measure(VMArray<Object*>{ &uniform, nullptr }, readPosition, readScale);
	check(Clipboard::SelectionGeometry::PlanScale(uniformWithNull, -1.0F, true).factor == 2.0F,
		"uniform restore factor is independent of null-row averaging");
	for (const float invalid : { 0.0F, -1.0F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
		const Geometry geometry{ {}, {}, invalid, invalid, invalid };
		const auto plan = Clipboard::SelectionGeometry::PlanScale(geometry, -1.0F, true);
		check(plan.restore && !plan.movePositions && plan.factor == 1.0F && plan.ObjectScale(invalid) == 1.0F,
			"invalid uniform scale cannot produce an invalid spacing factor or target scale");
	}
	const auto alreadyRestored = Clipboard::SelectionGeometry::PlanScale(Geometry{}, -1.0F, true);
	check(alreadyRestored.restore && !alreadyRestored.movePositions && alreadyRestored.ObjectScale(1.0F) == 1.0F,
		"empty or restored group is stable on a second restore");
	Object one{ { -5.0F, 10.0F, 4.0F }, 2.5F };
	Object two{ { 7.0F, -2.0F, 12.0F }, 0.25F };
	Object three{ { 6.0F, 9.0F, -8.0F }, 1.0F };
	verify({ &one });
	verify({ &one, &two, &three });
	verify({ &two, &three, &one });
	verify({ &one, nullptr, &two });
	const auto nullRow = Clipboard::SelectionGeometry::Measure(VMArray<Object*>{ &one, nullptr, &two }, readPosition, readScale);
	check(Same(nullRow.averageScale, 2.75F / 3.0F), "null later row retains whole-array averaging denominator");
	const auto asymmetric = Clipboard::SelectionGeometry::Measure(VMArray<Object*>{ &one, &two, &three }, readPosition, readScale);
	check(Same(asymmetric.Center(), Point{ 1.0F, 4.0F, 2.0F }), "center is bounds midpoint, not arithmetic mean");
	check(Same(asymmetric.Area(), Point{ 12.0F, 12.0F, 20.0F }), "area is position extent, not object dimensions");

	Object zero{ { -0.0F, -0.0F, -0.0F }, -0.0F };
	Object huge{ { std::numeric_limits<float>::max(), 1.0e20F, -1.0e20F }, 1.0e20F };
	Object opposite{ { -std::numeric_limits<float>::max(), -1.0e20F, 1.0e20F }, -1.0e20F };
	Object tiny{ { 1.0F, 0.125F, -0.125F }, 0.00001F };
	Object exceptional{ { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), -0.0F },
		std::numeric_limits<float>::quiet_NaN() };
	verify({ &zero });
	verify({ &zero, &one });
	verify({ &huge });
	verify({ &huge, &one, &opposite });
	verify({ &tiny, &huge });
	verify({ &exceptional, &one, &two });
	verify({ &one, &exceptional, &two });

	std::vector<Object> storage;
	storage.reserve(10000);
	VMArray<Object*> many;
	for (std::uint32_t index = 0; index < 10000; ++index) {
		storage.push_back({ { static_cast<float>(index % 101) - 50.0F,
			static_cast<float>(index % 67) - 33.0F, static_cast<float>(index % 29) - 14.0F },
			static_cast<float>(index % 999 + 1) * 0.01F });
		many.push_back(&storage.back());
	}
	verify(many);
	std::uint32_t fullPositions{}, fullScales{}, centerPositions{}, centerScales{};
	const auto full = Clipboard::SelectionGeometry::Measure(many,
		[&](const Object* object) { ++fullPositions; return object->position; },
		[&](const Object* object) { ++fullScales; return object->scale; });
	const auto center = Clipboard::SelectionGeometry::Measure<false>(many,
		[&](const Object* object) { ++centerPositions; return object->position; },
		[&](const Object* object) { ++centerScales; return object->scale; });
	check(Same(full.Center(), center.Center()), "10000-object center matches full geometry");
	check(fullPositions == 10000 && centerPositions == fullPositions, "center reads each position once");
	check(fullScales == 10000 && centerScales == 0, "center eliminates all scale reads and accumulation");
	TestScaleConstraints(check);
	std::cout << "Selection geometry: " << checks << " checks, " << failures << " failures\n"
		<< "10000-object center work: position reads " << fullPositions << " -> " << centerPositions
		<< ", scale reads " << fullScales << " -> " << centerScales << '\n';
	return failures ? 1 : 0;
}
