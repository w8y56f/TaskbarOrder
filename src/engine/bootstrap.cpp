// SPDX-License-Identifier: GPL-3.0-only
#include "engine/reorder.hpp"
#include "common/windows.hpp"
#include <cstring>

// A thread-specific Windows hook loads this DLL only into the verified taskbar
// thread. DllMain performs no work. There is no global injector/service.
extern "C" __declspec(dllexport) LRESULT CALLBACK TaskbarOrderBootstrap(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(lp);
        static const UINT commandMessage = RegisterWindowMessageW(tbo::kCommandMessage);
        if (commandMessage && message->message == commandMessage) {
            try {
                const auto controllerPid = static_cast<DWORD>(message->wParam);
                DWORD ownerSession{}, thisSession{}, taskbarPid{};
                const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
                const DWORD tid = GetWindowThreadProcessId(taskbar, &taskbarPid);
                if (message->hwnd != taskbar || taskbarPid != GetCurrentProcessId() || tid != GetCurrentThreadId() ||
                    !ProcessIdToSessionId(controllerPid, &ownerSession) || !ProcessIdToSessionId(GetCurrentProcessId(), &thisSession) || ownerSession != thisSession)
                    return CallNextHookEx(nullptr, code, wp, lp);
                tbo::Handle mapping(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, tbo::mapping_name(controllerPid).c_str()));
                if (!mapping) return CallNextHookEx(nullptr, code, wp, lp);
                auto* shared = static_cast<tbo::SharedCommand*>(MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(tbo::SharedCommand)));
                if (!shared) return CallNextHookEx(nullptr, code, wp, lp);
                if (shared->magic == tbo::kMagic && shared->size == sizeof(*shared) &&
                    shared->controllerPid == controllerPid && shared->explorerPid == GetCurrentProcessId()) {
                    std::wstring status;
                    auto result = tbo::Status::Failed;
                    try {
                        if (shared->command == tbo::Command::Start) {
                            // Keep trampolines and callbacks alive until Explorer exits.
                            HMODULE pinned{};
                            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                reinterpret_cast<LPCWSTR>(&TaskbarOrderBootstrap), &pinned)) throw std::runtime_error("Cannot pin hook module");
                            result = tbo::engine::start(shared->plan, controllerPid, status) ? tbo::Status::Active : tbo::Status::Failed;
                        } else if (shared->command == tbo::Command::Stop) {
                            tbo::engine::stop();
                            status = L"调序已停用。DLL 保持空闲驻留，随 Explorer 退出释放。";
                            result = tbo::Status::Stopped;
                        }
                    } catch (const std::exception& e) {
                        status = L"初始化失败：";
                        for (const char* c = e.what(); *c; ++c) status += static_cast<wchar_t>(static_cast<unsigned char>(*c));
                    } catch (...) { status = L"初始化发生未知错误，未启用调序。"; }
                    wcsncpy_s(shared->message, status.c_str(), _TRUNCATE);
                    InterlockedExchange(&shared->status, static_cast<LONG>(result));
                    tbo::log(status, true);
                }
                UnmapViewOfFile(shared);
            } catch (...) { tbo::log(L"Bootstrap failed", true); }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
