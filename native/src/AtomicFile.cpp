// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#include "AtomicFile.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <string>
#include <utility>

namespace Clipboard::AtomicFile
{
	namespace
	{
		class Transaction
		{
		public:
			explicit Transaction(std::filesystem::path destination) : destination_(std::move(destination)) {}
			~Transaction()
			{
				if (handle_ != INVALID_HANDLE_VALUE) { CloseHandle(handle_); }
				if (created_) { DeleteFileW(temporary_.c_str()); }
			}
			bool Create()
			{
				if (destination_.empty() || destination_.filename().empty()) {
					error_ = ERROR_INVALID_NAME;
					return false;
				}
				static std::atomic<std::uint64_t> sequence{};
				for (unsigned attempt = 0; attempt < 64; ++attempt) {
					temporary_ = destination_;
					temporary_ += L".clipboard-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
						std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed)) + L".tmp";
					handle_ = CreateFileW(temporary_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
						CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
					if (handle_ != INVALID_HANDLE_VALUE) { created_ = true; return true; }
					error_ = GetLastError();
					if (error_ != ERROR_FILE_EXISTS && error_ != ERROR_ALREADY_EXISTS) { return false; }
				}
				return false;
			}
			bool Write(std::string_view contents)
			{
				while (!contents.empty()) {
					const auto count = static_cast<DWORD>((std::min)(contents.size(), std::size_t{ 1024 * 1024 }));
					DWORD written{};
					if (!WriteFile(handle_, contents.data(), count, &written, nullptr)) { return Failed(); }
					if (!written || written > count) { error_ = ERROR_WRITE_FAULT; return false; }
					contents.remove_prefix(written);
				}
				return true;
			}
			bool Flush() { return FlushFileBuffers(handle_) != 0 || Failed(); }
			bool Verify(std::string_view contents)
			{
				LARGE_INTEGER size{};
				if (!GetFileSizeEx(handle_, &size)) { return Failed(); }
				if (size.QuadPart < 0 || static_cast<std::uint64_t>(size.QuadPart) != contents.size()) {
					error_ = ERROR_INVALID_DATA; return false;
				}
				if (!SetFilePointerEx(handle_, {}, nullptr, FILE_BEGIN)) { return Failed(); }
				std::array<char, 64 * 1024> buffer{};
				while (!contents.empty()) {
					const auto count = static_cast<DWORD>((std::min)(contents.size(), buffer.size()));
					DWORD read{};
					if (!ReadFile(handle_, buffer.data(), count, &read, nullptr)) { return Failed(); }
					if (!read || read > count || std::memcmp(buffer.data(), contents.data(), read) != 0) {
						error_ = ERROR_INVALID_DATA; return false;
					}
					contents.remove_prefix(read);
				}
				return true;
			}
			bool Close()
			{
				if (!CloseHandle(handle_)) { return Failed(); }
				handle_ = INVALID_HANDLE_VALUE;
				return true;
			}
			bool Replace()
			{
				if (!MoveFileExW(temporary_.c_str(), destination_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
					return Failed();
				}
				created_ = false;
				return true;
			}
			std::uint32_t Error() const noexcept { return error_; }

		private:
			bool Failed() noexcept { error_ = GetLastError(); return false; }
			std::filesystem::path destination_, temporary_;
			HANDLE handle_{ INVALID_HANDLE_VALUE };
			std::uint32_t error_{};
			bool created_{};
		};
	}

	Result Write(const std::filesystem::path& destination, std::string_view contents)
	{
		Transaction file(destination);
		return Commit(file, contents);
	}
}
