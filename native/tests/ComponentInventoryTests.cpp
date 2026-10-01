// SPDX-License-Identifier: GPL-3.0-or-later
#include "ComponentInventory.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

int main()
{
    using namespace Clipboard::ComponentInventory;
    int checks = 0;
    auto check = [&](bool condition) {
        ++checks;
        if (!condition) { std::cerr << "Failed inventory check " << checks << '\n'; }
        return condition;
    };
    bool passed = true;
    std::vector<std::int32_t> balances(65, 1);
    std::vector<std::int32_t> observed;
    auto read = [&](std::int32_t value) { observed.push_back(value); return value; };
    passed &= check(SumBatch(balances, 0, read) == 32 && observed.size() == 32);
    observed.clear();
    passed &= check(SumBatch(balances, 32, read) == 32 && observed.size() == 32);
    observed.clear();
    balances[64] = 7;
    passed &= check(SumBatch(balances, 64, read) == 7 && observed.size() == 1);
    observed.clear();
    passed &= check(SumBatch(balances, 65, read) == 0 && observed.empty());
    passed &= check(SumBatch(balances, -1, read) == 0 && observed.empty());
    passed &= check(SumBatch(balances, std::numeric_limits<std::int32_t>::max(), read) == 0 && observed.empty());
    passed &= check(SumBatch(std::vector<int>{}, 0, read) == 0 && observed.empty());
    balances = { 3, 0, -1, 9 };
    passed &= check(SumBatch(balances, 0, read) == 12);
    passed &= check(observed == balances); // preserve source order, no skipped live reads
    balances[0] = 20;
    passed &= check(SumBatch(balances, 0, read) == 29); // no retained inventory cache
    balances = { std::numeric_limits<std::int32_t>::max(), 1 };
    passed &= check(SumBatch(balances, 0, read) == std::numeric_limits<std::int32_t>::max());
    passed &= check(RepresentCount(0) == 0);
    passed &= check(RepresentCount(42) == 42);
    passed &= check(RepresentCount(std::numeric_limits<std::uint32_t>::max()) == std::numeric_limits<std::int32_t>::max());
    passed &= check(UsableSourceCount(0) == 0);
    passed &= check(UsableSourceCount(2147483647u) == 2147483647);
    passed &= check(UsableSourceCount(2147483648u) == -1);
    passed &= check(UsableSourceCount(std::numeric_limits<std::uint32_t>::max()) == -1);
    std::cout << checks << " component inventory checks " << (passed ? "passed" : "failed") << '\n';
    return passed ? 0 : 1;
}
