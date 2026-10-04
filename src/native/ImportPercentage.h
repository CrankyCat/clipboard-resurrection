// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Clipboard::ImportProgress
{
    // UI accounting only. A row earns each unit once; repeated retry visits do
    // not advance the bar. Null/excluded rows have no units. No game ownership.
    class RowUnits
    {
    public:
        explicit RowUnits(std::size_t rows = 0) : required(rows), completed(rows) {}
        void Require(std::size_t row, std::uint8_t bits)
        {
            if (row >= required.size()) { return; }
            const auto added = static_cast<std::uint8_t>(bits & ~required[row]);
            required[row] |= bits;
            total += Count(added);
            done += Count(static_cast<std::uint8_t>(added & completed[row]));
        }
        void Complete(std::size_t row, std::uint8_t bits)
        {
            if (row >= completed.size()) { return; }
            done += Count(static_cast<std::uint8_t>(bits & required[row] & ~completed[row]));
            completed[row] |= bits;
        }
        void Omit(std::size_t row, std::uint8_t bits)
        {
            if (row >= required.size()) { return; }
            const auto removed = static_cast<std::uint8_t>(bits & required[row]);
            total -= Count(removed);
            done -= Count(static_cast<std::uint8_t>(removed & completed[row]));
            required[row] &= static_cast<std::uint8_t>(~bits);
        }
        [[nodiscard]] std::uint32_t Total() const { return total; }
        [[nodiscard]] std::uint32_t Done() const { return done; }
        [[nodiscard]] bool AllDone() const { return done == total; }
        [[nodiscard]] int Percent() const
        {
            // 100 is reserved for the caller's full phase barrier, including
            // other fast callbacks, validation and cancellation checks.
            return total ? static_cast<int>(std::min<std::uint64_t>(99, 100ULL * done / total)) : 0;
        }
    private:
        static std::uint32_t Count(std::uint8_t bits)
        {
            std::uint32_t count{};
            while (bits) { count += bits & 1; bits >>= 1; }
            return count;
        }
        std::vector<std::uint8_t> required, completed;
        std::uint32_t total{}, done{};
    };

    // Select the UI plan from the first actual network pass, before it processes
    // any row. This keeps the Papyrus contract and combined-mode weights intact.
    // The extra assembly unit is required only for split work. No engine refs.
    class PowerUnits
    {
    public:
        static constexpr std::uint8_t Network = 1, Animation = 2, Assembly = 4;
        explicit PowerUnits(std::size_t rows = 0) : units(rows), included(rows) {}
        void Require(std::size_t row)
        {
            if (planSelected || row >= included.size()) { return; }
            included[row] = true;
            units.Require(row, Network | Animation);
        }
        void SelectNetworkPlan(bool split)
        {
            if (planSelected) { return; }
            planSelected = true;
            if (split) {
                for (std::size_t row = 0; row < included.size(); ++row) {
                    if (included[row]) { units.Require(row, Assembly); }
                }
            }
        }
        void Complete(std::size_t row, std::uint8_t bit)
        {
            // An out-of-order or legacy caller cannot expand the denominator
            // after any credit has been supplied and make progress go backward.
            if (!planSelected) { SelectNetworkPlan(false); }
            units.Complete(row, bit);
        }
        [[nodiscard]] std::uint32_t Total() const { return units.Total(); }
        [[nodiscard]] std::uint32_t Done() const { return units.Done(); }
        [[nodiscard]] bool AllDone() const { return units.AllDone(); }
        [[nodiscard]] int Percent() const { return units.Percent(); }
    private:
        RowUnits units;
        std::vector<bool> included;
        bool planSelected{};
    };
}
