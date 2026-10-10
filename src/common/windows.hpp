// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "common/protocol.hpp"
#include <filesystem>
#include <string_view>
#include <vector>

namespace tbo {
class Handle {
public:
    explicit Handle(HANDLE h = nullptr) : h_(h) {}
    ~Handle() { if (h_ && h_ != INVALID_HANDLE_VALUE) CloseHandle(h_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return h_; }
    explicit operator bool() const { return h_ && h_ != INVALID_HANDLE_VALUE; }
private: HANDLE h_;
};
struct ModuleFile { std::filesystem::path path; ModuleIdentity identity; std::wstring pdb; };
struct Environment {
    HWND taskbar{};
    DWORD pid{}, tid{}, build{};
    std::array<ModuleFile, 2> modules;
};
std::filesystem::path exe_directory();
std::filesystem::path data_directory();
std::wstring error_text(DWORD code = GetLastError());
std::string utf8(std::wstring_view value);
void log(std::wstring_view value, bool engine = false) noexcept;
Environment inspect_environment();
ModuleFile inspect_module_file(const std::filesystem::path& path);
ModuleIdentity inspect_loaded_module(HMODULE module);
std::wstring pdb_key(const ModuleIdentity& id);
std::wstring describe_environment(const Environment& environment);
bool readable(const void* ptr, std::size_t bytes) noexcept;
bool writable(void* ptr, std::size_t bytes) noexcept;
}
