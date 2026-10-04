// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Clipboard::ResourceStreamABI
{
    // F4SE GameStreams.h/.cpp and the four-fixture audit agree on this 0x30
    // wrapper. Its internal Stream refcount offset changes from OG +0xC to
    // NG/AE +0x10. Let the engine's constructor/destructor own it entirely;
    // never instantiate CommonLib's owning stream member or release it twice.
    struct BufferInfo
    {
        void* buffer{};
        std::uint64_t fileSize{}, bufferAllocSize{}, bufferReadSize{}, pos{}, absolutePos{};
    };
    static_assert(sizeof(BufferInfo) == 0x30);

    struct Operations
    {
        void* (*construct)(void*, const char*, bool, void*, bool);
        void (*destroy)(void*);
        void (*getInfo)(void*, BufferInfo&);
        std::size_t (*read)(void*, void*, std::size_t);
    };
    class Stream
    {
    public:
        Stream(Operations operations, const char* path) : ops(operations) { ops.construct(storage.data(), path, false, nullptr, false); }
        ~Stream() { ops.destroy(storage.data()); }
        Stream(const Stream&) = delete;
        Stream& operator=(const Stream&) = delete;
        [[nodiscard]] bool Good() const noexcept
        {
            std::uintptr_t handle{};
            std::uint32_t error{};
            std::memcpy(&handle, storage.data() + 0x10, sizeof(handle));
            std::memcpy(&error, storage.data() + 0x28, sizeof(error));
            return handle != 0 && error == 0;
        }
        [[nodiscard]] std::uint64_t Size()
        {
            BufferInfo info;
            if (Good()) { ops.getInfo(storage.data(), info); }
            return info.fileSize;
        }
        std::size_t Read(void* data, std::size_t bytes) { return Good() ? ops.read(storage.data(), data, bytes) : 0; }
    private:
        alignas(8) std::array<std::byte, 0x30> storage{};
        Operations ops;
    };
}
