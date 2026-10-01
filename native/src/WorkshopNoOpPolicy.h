// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once
#include <cstdint>
#include <string_view>

namespace Clipboard::WorkshopNoOp
{
    enum class Kind : std::uint8_t { Unknown, Plain, WorkshopObject, Actor, Count };
    inline constexpr const char* kKindNames[]{ "unknown", "plain", "workshop-object", "actor" };

    // Full winning-resource SHA-256, not a plugin name/version claim. These
    // exact PEX bodies were inspected in WORKSHOP_STATIC_CALLBACK_AUDIT.
    [[nodiscard]] constexpr unsigned WorkshopRevision(std::string_view sha) noexcept
    {
        if (sha == "3A59DD9F4CE673949360E669A63119923B1F204BE345DD1890031E232DF1468C") { return 2411; }
        if (sha == "89F2427AFBD926E33C20C75A63D765A957469662AA5CE4EAC0612AC68A10C07D") { return 260; }
        return 0;
    }
    [[nodiscard]] constexpr unsigned ParentRevision(std::string_view sha) noexcept
    {
        if (sha == "60164EECB2D2B9CDED92258F482AC4E669EA095E4BD9FE19496878E16BD56BB0") { return 2411; }
        if (sha == "DF5F7E81B86181550C22E3835D5C0DCDBB1C2736C8FCA7386D96F2160A911AD3") { return 260; }
        return 0;
    }
    [[nodiscard]] constexpr bool CanOmit(Kind kind, bool compatibleReceiver) noexcept
    {
        return compatibleReceiver && kind == Kind::Plain;
    }

    enum class ReceiverKind : std::uint8_t { Unknown, Workshop, Vault };
    // This exact DLC06 body only overrides OnActivate/GetMaxWorkshopNPCs;
    // placed/moved and the WorkshopParent property are inherited unchanged.
    inline constexpr std::string_view kVaultWorkshopSHA{
        "5624521F82CEE6B9167660EF9E1039815A30A857F29BDBF4E7DD9139E7D1B68A"
    };
    template<class Type, class Classify, class GetParent, class ValidType>
    [[nodiscard]] const Type* AuditedWorkshopType(const Type* type, std::string_view vaultSHA,
        Classify classify, GetParent parent, ValidType valid)
    {
        if (!type || !valid(type)) { return nullptr; }
        if (classify(type) == ReceiverKind::Workshop) { return type; }
        if (classify(type) != ReceiverKind::Vault || vaultSHA != kVaultWorkshopSHA) { return nullptr; }
        const auto* base = parent(type);
        return base && valid(base) && classify(base) == ReceiverKind::Workshop ? base : nullptr;
    }

    // Run under the caller's attachment lock. Null, partially linked or
    // unfinished attachments cannot prove absence. Positive inherited
    // WorkshopObjectScript capability always preserves the normal path.
    template<class Attachments, class GetObject, class Ready, class GetType, class ValidType, class GetParent, class IsWorkshop>
    [[nodiscard]] Kind ClassifyAttachments(const Attachments& attachments, GetObject getObject, Ready ready,
        GetType getType, ValidType validType, GetParent parent, IsWorkshop isWorkshop)
    {
        bool any{}, unknown{};
        for (const auto& attached : attachments) {
            any = true;
            const auto* object = getObject(attached);
            if (!object || !ready(object)) { unknown = true; }
            auto* type = object ? getType(object) : nullptr;
            if (!type) { unknown = true; }
            unsigned depth{};
            for (; type && depth < 128; type = parent(type), ++depth) {
                if (!validType(type)) { unknown = true; break; }
                if (isWorkshop(type)) { return Kind::WorkshopObject; }
            }
            if (type) { unknown = true; }
        }
        return !any || unknown ? Kind::Unknown : Kind::Plain;
    }
}
