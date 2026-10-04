// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScaleConstraints.h"

template<class Check>
void TestScaleConstraints(Check&& check)
{
    using namespace Clipboard::ScaleConstraints;
    Selection hundred; hundred.Add(100);
    check(!hundred.CheckInput("12.345678", true), "fractional increase rejected before float conversion");
    check(!hundred.CheckInput("12.345678", false), "fractional decrease rejected before float conversion");
    check(hundred.CheckInput("12.000000", true).Target(100) == 112, "exact increase with trailing zeroes");
    check(hundred.CheckInput("12", false).Target(100) == 88, "exact decrease");
    for (const auto text : {"12.000001", "11.999999", "0.000001", "1000.000001", "1.0000000", "", "0", "-1", "nan", "1e2", "1,5", ".", "1."}) {
        check(!hundred.CheckInput(text, true), "invalid/near-integer text never rounds into approval");
    }
    Selection mixed; for (auto p : {50U,100U,150U}) { mixed.Add(p); }
    check(!mixed.CheckInput("1", true), "mixed group rejects fractional targets");
    const auto two = mixed.CheckInput("2", true);
    check(two && two.Target(50) == 51 && two.Target(100) == 102 && two.Target(150) == 153, "one common factor for mixed group");
    const auto choices = mixed.Choices(12345678, true);
    check(choices.step == 2000000 && choices.lower == 12000000 && choices.upper == 14000000, "nearby choices for mixed group");
    Selection twoHundred; twoHundred.Add(200);
    check(twoHundred.CheckInput(".5", true).Target(200) == 201, "fractional percentage can be exact");
    Selection recurring; recurring.Add(150);
    check(!recurring.CheckInput(".666666", true) && recurring.Choices(666666, true).step == 2000000, "recurring rational is not an exact six-place decimal");
    Selection coprime; coprime.Add(99); coprime.Add(100);
    check(coprime.Choices(10 * kMicros, false).maximum == 0, "no exact decrease for coprime scales");
    Selection limits; limits.Add(1); limits.Add(1000);
    check(!limits.CheckInput("1", true) && !limits.CheckInput("1", false), "whole group limits reject instead of clamping");
    check(!limits.Choices(kMicros,true).maximum && !limits.Choices(kMicros,false).maximum, "no alternatives at both limits");
    check(!Selection{}.CheckInput("1",true), "empty selection fails closed");
    Selection invalid; invalid.Add(100); invalid.Add(0);
    check(!invalid.CheckInput("1",true), "invalid member fails whole group");
    for (auto factor : {0.0F, -1.0F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 1.12345678F, .87654322F}) {
        check(!hundred.CheckFactor(factor), "checked relative dispatch rejects invalid factors");
    }
    check(PercentText(1250000) == "1.25" && PercentText(1000000) == "1" && PercentText(1) == "0.000001", "exact suggestion formatting");
    // Independent integer arithmetic oracle over all engine starting sizes and
    // a selection of integer/fractional deltas, including boundaries.
    for (std::uint32_t current = 1; current <= 1000; ++current) {
        Selection single; single.Add(current);
        for (const auto micros : {1ULL, 100000ULL, 500000ULL, 1000000ULL, 2000000ULL, 12345678ULL, 25000000ULL, 99000000ULL, 100000000ULL, 900000000ULL, 1000000000ULL}) {
            for (bool increase : {false,true}) {
                const auto signedDelta = static_cast<std::int64_t>(micros) * (increase ? 1 : -1);
                const auto product = static_cast<std::int64_t>(current) * (100000000 + signedDelta);
                const bool expected = micros <= (increase ? 1000000000ULL : 99000000ULL) &&
                    product >= 100000000 && product <= 100000000000 && product % 100000000 == 0;
                const auto result = single.CheckInput(PercentText(micros), increase);
                check(static_cast<bool>(result) == expected, "integer oracle agrees with text validation");
                if (result) {
                    const auto amount = static_cast<float>(micros) / 1000000.0F;
                    const auto papyrusFactor = increase ? amount / 100.0F + 1.0F : 1.0F - amount / 100.0F;
                    const auto recovered = single.CheckFactor(papyrusFactor);
                    check(recovered && recovered.Target(current) == result.Target(current), "float dispatch retains exact target");
                    const auto applied = static_cast<float>(result.Target(current)) / 100.0F;
                    check(static_cast<std::uint32_t>(std::floor(applied * 100.0F + .5F)) == result.Target(current), "engine setter recovers exact percent");
                }
            }
        }
        for (bool increase : {false,true}) {
            const auto offered = single.Choices(12345678, increase);
            for (const auto suggestion : {offered.lower, offered.upper, offered.maximum}) {
                check(!suggestion || single.CheckInput(PercentText(suggestion),increase), "every offered alternative is valid");
            }
        }
    }
    // Group membership/size can change while the input menu or latent call waits.
    check(hundred.CheckFactor(1.01F) && !mixed.CheckFactor(1.01F), "fresh mixed snapshot rejects formerly valid factor");
    check(hundred.CheckFactor(2.0F) && !limits.CheckFactor(2.0F), "fresh limit snapshot rejects formerly valid factor");
}
