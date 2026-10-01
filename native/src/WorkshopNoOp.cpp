// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#include "PCH.h"
#include "WorkshopNoOp.h"
#include "ResourceStreamABI.h"
#include <bcrypt.h>

namespace Clipboard::WorkshopNoOp
{
    namespace
    {
        [[nodiscard]] std::string ResourceSHA(const char* path)
        {
            // The engine resolves the winning loose/archive resource. Mod
            // manager filesystem paths cannot establish that precedence.
            using ResourceStreamABI::Operations;
            const REL::Relocation<decltype(Operations::construct)> construct{ REL::ID(1198116, 2269830) };
            const REL::Relocation<decltype(Operations::destroy)> destroy{ REL::ID(1516202, 2269832) };
            const REL::Relocation<decltype(Operations::getInfo)> getInfo{ REL::ID(265501, 2269836) };
            const REL::Relocation<decltype(Operations::read)> read{ REL::ID(424286, 2269839) };
            ResourceStreamABI::Stream stream{ { construct.get(), destroy.get(), getInfo.get(), read.get() }, path };
            constexpr std::uint32_t maximumBytes = 4 * 1024 * 1024;
            const auto size = stream.Size();
            if (!stream.Good() || !size || size > maximumBytes) { return {}; }
            std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
            if (stream.Read(bytes.data(), bytes.size()) != bytes.size() || !stream.Good()) { return {}; }
            std::array<unsigned char, 32> digest{};
            struct HashContext
            {
                BCRYPT_ALG_HANDLE algorithm{};
                BCRYPT_HASH_HANDLE hash{};
                ~HashContext()
                {
                    if (hash) { BCryptDestroyHash(hash); }
                    if (algorithm) { BCryptCloseAlgorithmProvider(algorithm, 0); }
                }
            } context;
            // Preserve the project's Windows 7 API target: the one-shot hash
            // and algorithm pseudo-handles require a newer Windows target.
            if (BCryptOpenAlgorithmProvider(&context.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
                BCryptCreateHash(context.algorithm, &context.hash, nullptr, 0, nullptr, 0, 0) < 0 ||
                BCryptHashData(context.hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0) < 0 ||
                BCryptFinishHash(context.hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) { return {}; }
            constexpr char digits[] = "0123456789ABCDEF";
            std::string result;
            result.reserve(digest.size() * 2);
            for (const auto byte : digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
            return result;
        }

        [[nodiscard]] bool Ready(const RE::BSScript::Object* object) noexcept
        {
            return object && object->IsConstructed() && object->IsInitialized() && object->IsValid() &&
                object->type && object->type->Valid();
        }
    }

    Resources InspectResources() noexcept
    {
        Resources result;
        try {
            result.workshopSHA = ResourceSHA("Scripts\\WorkshopScript.pex");
            result.parentSHA = ResourceSHA("Scripts\\WorkshopParentScript.pex");
            result.workshopRevision = WorkshopRevision(result.workshopSHA);
            result.parentRevision = ParentRevision(result.parentSHA);
            result.vaultWorkshopSHA = ResourceSHA("Scripts\\DLC06\\VaultWorkshopScript.pex");
        } catch (...) {} // Unknown/resource failure always keeps the original path.
        return result;
    }

    bool CompatibleReceiver(RE::BSScript::IVirtualMachine& vm, RE::TESObjectREFR* workshop, const Resources& resources) noexcept
    {
        try {
            if (!resources.Compatible() || !workshop || workshop->IsDeleted()) { return false; }
            const auto& handles = vm.GetObjectHandlePolicy();
            const auto handle = handles.GetHandleForObject(RE::BSScript::GetVMTypeID<RE::TESObjectREFR>(), workshop);
            if (handle == handles.EmptyHandle() || !handles.IsHandleObjectAvailable(handle)) { return false; }
            RE::BSTSmartPointer<RE::BSScript::Object> object;
            // Identical lookup to PackVariable's actual bound dispatch target:
            // allowConst=false, exactMatch=false. Only the separately audited
            // Vault receiver may inherit this known workshop implementation.
            if (!vm.FindBoundObject(handle, "ObjectReference", false, object, false) || !Ready(object.get()) ||
                (!object->currentState.empty() && object->currentState != "Initialized")) { return false; }
            const auto* workshopType = AuditedWorkshopType(object->type.get(), resources.vaultWorkshopSHA,
                [](const auto* type) {
                    if (type->name == "WorkshopScript") { return ReceiverKind::Workshop; }
                    if (type->name == "DLC06:VaultWorkshopScript") { return ReceiverKind::Vault; }
                    return ReceiverKind::Unknown;
                },
                [](const auto* type) { return type->parentTypeInfo.get(); },
                [](const auto* type) { return type->Valid(); });
            if (!workshopType) { return false; }

            // The audited WorkshopParent property is auto const. Validate its
            // backing index before reading it; no getter invocation or VM task.
            static_assert(offsetof(RE::BSScript::Object, currentState) == 0x10);
            static_assert(offsetof(RE::BSScript::Object, variables) == 0x30);
            static_assert(offsetof(RE::BSScript::ObjectTypeInfo, data) == 0x50);
            // GetPropertyIndex searches only this declaring type. As in the
            // pinned Object::GetProperty, an inherited autoVarIndex addresses
            // the same object's variables, without a derived-variable offset.
            const auto index = workshopType->GetPropertyIndex(RE::BSFixedString{ "WorkshopParent" });
            if (index >= object->type->GetVariableCount()) { return false; }
            const auto& value = object->variables[index];
            if (!value.is<RE::BSScript::Object>()) { return false; }
            const auto parent = RE::BSScript::get<RE::BSScript::Object>(value);
            return Ready(parent.get()) && parent->type->name == "WorkshopParentScript" && parent->currentState.empty();
        } catch (...) { return false; }
    }

    Kind Classify(RE::BSScript::IVirtualMachine& vm, RE::TESObjectREFR* reference) noexcept
    {
        try {
            if (!reference || reference->IsDeleted() || !reference->data.objectReference) { return Kind::Unknown; }
            if (reference->As<RE::Actor>()) { return Kind::Actor; } // Preserve faction/turret handling.
            const auto& handles = vm.GetObjectHandlePolicy();
            const auto handle = handles.GetHandleForObject(RE::BSScript::GetVMTypeID<RE::TESObjectREFR>(), reference);
            if (handle == handles.EmptyHandle() || !handles.IsHandleObjectAvailable(handle)) { return Kind::Unknown; }
            const RE::BSFixedString workshopObjectName{ "WorkshopObjectScript" };
            auto& internal = static_cast<RE::BSScript::Internal::VirtualMachine&>(vm);
            // Reuse the audited, locked attachment layout. Only an enum escapes;
            // no borrowed objects, type pointers or negative cache survive it.
            static_assert(offsetof(RE::BSScript::Internal::VirtualMachine, attachedScriptsLock) == 0xBDF8);
            static_assert(offsetof(RE::BSScript::Internal::VirtualMachine, attachedScripts) == 0xBE00);
            const RE::BSAutoLock lock{ internal.attachedScriptsLock };
            const auto found = internal.attachedScripts.find(handle);
            if (found == internal.attachedScripts.end()) { return Kind::Unknown; }
            return ClassifyAttachments(found->second,
                [](const auto& attached) { return attached.get(); }, Ready,
                [](const auto* object) { return object->type.get(); },
                [](const auto* type) { return type->Valid(); },
                [](const auto* type) { return type->parentTypeInfo.get(); },
                [&](const auto* type) { return type->name == workshopObjectName; });
        } catch (...) { return Kind::Unknown; }
    }
}
