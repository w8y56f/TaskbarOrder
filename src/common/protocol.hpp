// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <string>

namespace tbo {
inline constexpr wchar_t kCommandMessage[] = L"TaskbarOrder.0.1.Command";
inline constexpr wchar_t kHookFile[] = L"TaskbarOrderHook-0.1.dll";
inline constexpr std::uint32_t kMagic = 0x54424F31;
inline constexpr std::size_t kSymbolCount = 16;
enum class Command : std::uint32_t { Start = 1, Stop = 2 };
enum class Status : LONG { Pending = 0, Active = 1, Stopped = 2, Failed = 3 };
struct ModuleIdentity {
    std::uint32_t timestamp{};
    std::uint32_t imageSize{};
    GUID pdbGuid{};
    std::uint32_t pdbAge{};
    std::uint16_t machine{};
    std::uint16_t reserved{};
};
struct Plan {
    std::array<ModuleIdentity, 2> modules{};
    std::array<std::uint32_t, kSymbolCount> rvas{};
};
struct SharedCommand {
    std::uint32_t magic{kMagic};
    std::uint32_t size{sizeof(SharedCommand)};
    std::uint32_t controllerPid{};
    std::uint32_t explorerPid{};
    Command command{Command::Start};
    volatile LONG status{static_cast<LONG>(Status::Pending)};
    Plan plan{};
    wchar_t message[384]{};
};
inline std::wstring mapping_name(DWORD pid) {
    return L"Local\\TaskbarOrder.Command." + std::to_wstring(pid);
}
inline std::wstring enable_event_name(DWORD pid) {
    return L"Local\\TaskbarOrder.Enable." + std::to_wstring(pid);
}
inline bool same_identity(const ModuleIdentity& a, const ModuleIdentity& b) {
    return a.timestamp == b.timestamp && a.imageSize == b.imageSize &&
        a.machine == b.machine && a.pdbAge == b.pdbAge &&
        InlineIsEqualGUID(a.pdbGuid, b.pdbGuid);
}
}
