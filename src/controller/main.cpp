// SPDX-License-Identifier: GPL-3.0-only
#include "common/windows.hpp"
#include "version.hpp"
#include "controller/symbols.hpp"
#include <objbase.h>
#include <shellapi.h>
#include <commctrl.h>
#include <atomic>
#include <fstream>
#include <memory>
#include <thread>
#include <stdexcept>

namespace {
using namespace tbo;
constexpr UINT kProgress = WM_APP + 1, kDone = WM_APP + 2, kTray = WM_APP + 3;
constexpr int kInspect = 101, kPrepare = 102, kStart = 103, kStop = 104, kLogs = 105, kShow = 106, kExit = 107, kAbout = 108;
constexpr int kAppIcon = 201;
HWND mainWindow{}, output{};
HICON appIcon{}, smallIcon{};
std::thread worker;
std::atomic<bool> canceled{};
bool busy{}, closing{}, activeEngine{}, trayPresent{};
DWORD activePid{};
HFONT font{};
HANDLE enableEvent{};
std::wstring report;

std::wstring widen(const char* text) {
    const int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (count <= 0) return L"Unknown error";
    std::wstring result(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), count);
    result.pop_back(); return result;
}
void append(const std::wstring& text) {
    report += text + L"\r\n";
    SetWindowTextW(output, report.c_str());
    SendMessageW(output, EM_SETSEL, report.size(), report.size());
    SendMessageW(output, EM_SCROLLCARET, 0, 0);
    log(text);
}
void buttons() {
    for (int id : {kInspect, kPrepare, kStart, kStop})
        EnableWindow(GetDlgItem(mainWindow, id), !busy && (id == kStop ? activeEngine : !activeEngine));
}
void tray(bool remove = false) {
    NOTIFYICONDATAW icon{sizeof(icon)};
    icon.hWnd = mainWindow; icon.uID = 1;
    if (remove) { Shell_NotifyIconW(NIM_DELETE, &icon); trayPresent = false; return; }
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = kTray;
    icon.hIcon = smallIcon;
    wcscpy_s(icon.szTip, activeEngine ? L"TaskbarOrder — 调序已启用" : L"TaskbarOrder — 未启用");
    trayPresent = Shell_NotifyIconW(trayPresent ? NIM_MODIFY : NIM_ADD, &icon) != FALSE;
}
struct Result { bool success{}; int operation{}; DWORD pid{}; std::wstring text; };
void post_progress(std::wstring text) {
    auto value = std::make_unique<std::wstring>(std::move(text));
    if (PostMessageW(mainWindow, kProgress, 0, reinterpret_cast<LPARAM>(value.get()))) value.release();
}

std::wstring send_command(const Environment& env, Command command, const Plan& plan = {}) {
    if (command == Command::Stop) ResetEvent(enableEvent);
    else if (!SetEvent(enableEvent)) throw std::runtime_error(utf8(error_text()));
    struct Activation {
        bool committed{};
        ~Activation() { if (!committed) ResetEvent(enableEvent); }
    } activation;
    // All executable loading is from the application directory. Windows supplies
    // transitive OS libraries; no current-directory DLL search is needed.
    const auto dllPath = exe_directory() / kHookFile;
    HMODULE library = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) throw std::runtime_error("Cannot load engine DLL: " + utf8(error_text()));
    struct Library { HMODULE h; ~Library() { FreeLibrary(h); } } scopedLibrary{library};
    auto callback = reinterpret_cast<HOOKPROC>(GetProcAddress(library, "TaskbarOrderBootstrap"));
    if (!callback) throw std::runtime_error("Engine bootstrap export not found");
    const auto pid = GetCurrentProcessId();
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedCommand), mapping_name(pid).c_str()));
    if (!mapping) throw std::runtime_error(utf8(error_text()));
    if (GetLastError() == ERROR_ALREADY_EXISTS) throw std::runtime_error("Command mapping already exists; retry after the previous command finishes");
    auto* shared = static_cast<SharedCommand*>(MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedCommand)));
    if (!shared) throw std::runtime_error(utf8(error_text()));
    struct View { void* p; ~View() { UnmapViewOfFile(p); } } view{shared};
    new (shared) SharedCommand{};
    shared->controllerPid = pid; shared->explorerPid = env.pid; shared->command = command; shared->plan = plan;
    HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, callback, library, env.tid);
    if (!hook) throw std::runtime_error("Cannot attach to taskbar thread: " + utf8(error_text()));
    struct Hook { HHOOK h; ~Hook() { UnhookWindowsHookEx(h); } } scopedHook{hook};
    const UINT message = RegisterWindowMessageW(kCommandMessage);
    if (!message) throw std::runtime_error(utf8(error_text()));
    DWORD_PTR ignored{};
    if (!SendMessageTimeoutW(env.taskbar, message, pid, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 8000, &ignored))
        throw std::runtime_error("Taskbar command timed out. Exit this controller to deactivate any late start, then retry.");
    auto status = static_cast<Status>(InterlockedCompareExchange(&shared->status, 0, 0));
    if (status != (command == Command::Start ? Status::Active : Status::Stopped))
        throw std::runtime_error(shared->message[0] ? utf8(shared->message) : "Explorer did not acknowledge the command");
    activation.committed = true;
    return shared->message;
}

void begin(int operation) {
    if (busy || closing) return;
    if (operation == kStop) ResetEvent(enableEvent);
    busy = true; canceled = false; buttons();
    append(operation == kPrepare ? L"准备符号：首次需要访问 Microsoft 符号服务器；缓存匹配后可离线启用。" : L"正在处理 …");
    worker = std::thread([operation] {
        auto result = std::make_unique<Result>(); result->operation = operation;
        try {
            auto env = inspect_environment(); result->pid = env.pid;
            if (operation == kInspect) {
                result->text = describe_environment(env);
                std::ofstream out(data_directory() / L"environment.txt", std::ios::binary | std::ios::trunc);
                out << utf8(result->text);
                if (!out) throw std::runtime_error("Cannot write environment report");
                std::ofstream json(data_directory() / L"environment.json", std::ios::trunc);
                json << "{\n  \"format\": 1,\n  \"architecture\": \"x64\",\n  \"build\": " << env.build << ",\n  \"modules\": [\n";
                for (unsigned i = 0; i < 2; ++i) {
                    const auto& mod = env.modules[i];
                    json << "    {\"module\": \"" << utf8(mod.path.filename().wstring()) << "\", \"pdb\": \""
                         << utf8(mod.pdb) << "\", \"key\": \"" << utf8(pdb_key(mod.identity)) << "\"}"
                         << (i == 0 ? ",\n" : "\n");
                }
                json << "  ]\n}\n";
                if (!json) throw std::runtime_error("Cannot write offline symbol request");
                result->text += L"环境报告已写入 environment.txt / environment.json（打开日志可找到）。";
            } else if (operation == kPrepare) {
                prepare_symbols(env, true, [](std::wstring message) { post_progress(std::move(message)); }, [] { return canceled.load(); });
                result->text = L"符号已匹配并缓存。可以点击“启用调序”。这不代表窗口交互已验证。";
            } else if (operation == kStart) {
                auto plan = load_plan(env);
                if (canceled) throw std::runtime_error("Canceled");
                result->text = send_command(env, Command::Start, plan);
            } else if (operation == kStop) {
                result->text = send_command(env, Command::Stop);
            }
            result->success = true;
        } catch (const std::exception& e) { result->text = L"未完成：" + widen(e.what()); }
        catch (...) { result->text = L"未完成：未知异常。请查看日志。"; }
        if (PostMessageW(mainWindow, kDone, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
    });
}
void finish_close() {
    if (activeEngine) {
        // The controller owns the engine lifetime. Even if Explorer is hung,
        // the engine checks this process handle before any subsequent drag.
        try { auto env = inspect_environment(); if (env.pid == activePid) send_command(env, Command::Stop); }
        catch (...) {}
    }
    tray(true);
    DestroyWindow(mainWindow);
}
void request_exit() {
    if (closing) return;
    ResetEvent(enableEvent);
    closing = true; canceled = true;
    if (busy) append(L"正在结束当前操作后退出；网络等待可能需要几十秒。");
    else finish_close();
}
void show_about(HWND window) {
    const std::wstring text = std::wstring(L"TaskbarOrder\r\n\r\n版本：") + kVersion +
        L"\r\nCommit ID：" + kCommitId + L"\r\n\r\nPowered by Stone Wang";
    MSGBOXPARAMSW box{sizeof(box)};
    box.hwndOwner = window;
    box.hInstance = GetModuleHandleW(nullptr);
    box.lpszText = text.c_str(); box.lpszCaption = L"关于 TaskbarOrder";
    box.dwStyle = MB_OK | MB_USERICON;
    box.lpszIcon = MAKEINTRESOURCEW(kAppIcon);
    MessageBoxIndirectW(&box);
}
void layout(HWND window) {
    RECT r{}; GetClientRect(window, &r);
    const int margin = 14, height = 34;
    const int width = (r.right - 2 * margin - 4 * 8) / 5;
    for (int i = 0; i < 5; ++i) MoveWindow(GetDlgItem(window, kInspect + i), margin + i * (width + 8), margin, width, height, TRUE);
    MoveWindow(output, margin, margin + height + 12, r.right - 2 * margin, r.bottom - 2 * margin - height - 12, TRUE);
}
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    static const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (message == taskbarCreated) {
        trayPresent = false; tray();
        if (!trayPresent) ShowWindow(window, SW_RESTORE);
        return 0;
    }
    switch (message) {
    case WM_CREATE: {
        mainWindow = window;
        HMENU menu = CreateMenu(), settings = CreatePopupMenu();
        AppendMenuW(settings, MF_STRING, kAbout, L"关于(&A)…");
        AppendMenuW(settings, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(settings, MF_STRING, kExit, L"退出(&X)");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(settings), L"设置(&S)");
        SetMenu(window, menu);
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
        font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const wchar_t* labels[]{L"检测环境", L"准备符号", L"启用调序", L"停用调序", L"打开日志"};
        for (int i = 0; i < 5; ++i) {
            auto button = CreateWindowW(L"BUTTON", labels[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInspect + i)), nullptr, nullptr);
            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        output = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        SendMessageW(output, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        append(std::wstring(L"TaskbarOrder ") + kVersion + L" — 实验原型\r\n首次：检测环境 → 准备符号 → 启用调序。以后缓存匹配时，直接启用即可。\r\n当前目标：Win11 x64 原生任务栏；悬停应用图标后，在弹出的预览中用左键拖动卡片调序。\r\n已加入经典预览和 25H2/XAML 预览两条路径；你的 26200.9445 将使用后者。最小化或点击 × 会收进通知区域；从“设置 → 退出”或托盘菜单退出会停用调序。\r\n");
        buttons(); tray(); SetTimer(window, 1, 2000, nullptr);
        PostMessageW(window, WM_COMMAND, kInspect, 0);
        return 0;
    }
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED && trayPresent) ShowWindow(window, SW_HIDE);
        else layout(window);
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {720, 380}; return 0;
    case WM_COMMAND:
        if (LOWORD(wp) >= kInspect && LOWORD(wp) <= kStop) begin(LOWORD(wp));
        else if (LOWORD(wp) == kLogs) {
            try { ShellExecuteW(window, L"open", data_directory().c_str(), nullptr, nullptr, SW_SHOWNORMAL); } catch (...) {}
        } else if (LOWORD(wp) == kShow) { ShowWindow(window, SW_RESTORE); SetForegroundWindow(window); }
        else if (LOWORD(wp) == kAbout) show_about(window);
        else if (LOWORD(wp) == kExit) request_exit();
        return 0;
    case kProgress: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp)); append(*text); return 0;
    }
    case kDone: {
        std::unique_ptr<Result> result(reinterpret_cast<Result*>(lp));
        if (worker.joinable()) worker.join();
        busy = false;
        if (result->success && result->operation == kStart) { activeEngine = true; activePid = result->pid; }
        if (result->operation == kStop) activeEngine = false;
        append(result->text); buttons(); tray();
        if (closing) finish_close();
        return 0;
    }
    case kTray:
        if (lp == WM_LBUTTONDBLCLK) { ShowWindow(window, SW_RESTORE); SetForegroundWindow(window); }
        else if (lp == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, kShow, L"显示窗口");
            AppendMenuW(menu, MF_STRING | ((activeEngine && !busy) ? 0 : MF_GRAYED), kStop, L"停用调序");
            AppendMenuW(menu, MF_STRING, kAbout, L"关于 TaskbarOrder");
            AppendMenuW(menu, MF_STRING, kExit, L"退出");
            POINT pos{}; GetCursorPos(&pos); SetForegroundWindow(window);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pos.x, pos.y, 0, window, nullptr);
            DestroyMenu(menu); PostMessageW(window, WM_NULL, 0, 0);
        }
        return 0;
    case WM_TIMER:
        if (activeEngine && !busy) {
            DWORD pid{}; GetWindowThreadProcessId(FindWindowW(L"Shell_TrayWnd", nullptr), &pid);
            if (pid != activePid) {
                activeEngine = false; buttons(); tray();
                append(L"Explorer 已退出或重启。调序已停用；请重新检测并手动启用。");
            }
        }
        return 0;
    case WM_CLOSE:
        tray();
        if (trayPresent) ShowWindow(window, SW_HIDE);
        else MessageBoxW(window, L"暂时无法显示通知区域图标，窗口将保持打开。可通过“设置 → 退出”结束程序。", L"TaskbarOrder", MB_OK | MB_ICONINFORMATION);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp) { ResetEvent(enableEvent); canceled = true; tray(true); }
        return 0;
    case WM_DESTROY: KillTimer(window, 1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    tbo::Handle singleton(CreateMutexW(nullptr, FALSE, tbo::kControllerMutex));
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"TaskbarOrder 已在运行，请查看托盘。", L"TaskbarOrder", MB_OK); return 0;
    }
    tbo::Handle gate(CreateEventW(nullptr, TRUE, FALSE, tbo::enable_event_name(GetCurrentProcessId()).c_str()));
    if (!gate || GetLastError() == ERROR_ALREADY_EXISTS) return 1;
    enableEvent = gate.get();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW cls{}; cls.lpfnWndProc = window_proc; cls.hInstance = instance;
    cls.lpszClassName = L"TaskbarOrder.Controller"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    appIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(kAppIcon), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    smallIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(kAppIcon), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    cls.hIcon = appIcon; cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&cls);
    const std::wstring title = std::wstring(L"TaskbarOrder ") + tbo::kVersion + L" — 独立任务栏调序原型";
    auto window = CreateWindowW(cls.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 860, 500, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show); UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (worker.joinable()) worker.join();
    CoUninitialize();
    return 0;
}
