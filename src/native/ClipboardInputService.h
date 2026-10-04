// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace RE { class TESObjectREFR; }

namespace Clipboard::InputService
{
    inline constexpr std::int32_t kProtocol = 1;
    struct View
    {
        std::string token, header, initial, acceptLabel, cancelLabel, errorLabel, controllerHelp;
        std::int32_t inputType{}, maxChars{}, minimum{}, maximum{};
    };

    bool Available();
    std::string Begin(RE::TESObjectREFR* owner, std::string_view header, std::string_view initial,
        std::int32_t inputType, std::int32_t maxChars, std::int32_t minimum, std::int32_t maximum);
    std::int32_t State(std::string_view token);
    std::string Result(std::string_view token);
    bool Finished(std::string_view token);
    bool Acknowledge(std::string_view token);
    void Cancel(RE::TESObjectREFR* owner);
    void Abandon(std::string_view token);
    void Reset();

    // UI adapter callbacks. State changes commit before notifications; no
    // engine/UI/Papyrus calls execute with the coordinator's mutex held.
    bool Opening(std::string_view token);
    bool Ready(std::string_view token, std::int32_t protocol);
    bool Submit(std::string_view token, std::string_view value, bool cancelled);
    void Closed(std::string_view token);
    void Failed(std::string_view token, std::string_view reason);
}
