// SPDX-License-Identifier: GPL-3.0-only
#include "common/windows.hpp"
#include <tlhelp32.h>
#include <shlobj.h>
#include <winternl.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <limits>

namespace tbo {
std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}
std::wstring error_text(DWORD code) {
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::wstring result = L"Win32 " + std::to_wstring(code) + L": " + (text ? text : L"Unknown error");
    LocalFree(text);
    return result;
}
std::filesystem::path exe_directory() {
    std::vector<wchar_t> path(32768);
    auto n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n == path.size()) throw std::runtime_error("Cannot locate executable");
    return std::filesystem::path(std::wstring(path.data(), n)).parent_path();
}
std::filesystem::path data_directory() {
    wchar_t* path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)))
        throw std::runtime_error("Cannot locate LocalAppData");
    auto result = std::filesystem::path(path) / L"TaskbarOrder";
    CoTaskMemFree(path);
    std::filesystem::create_directories(result);
    return result;
}
void log(std::wstring_view value, bool engine) noexcept {
    try {
        SYSTEMTIME now{}; GetLocalTime(&now);
        wchar_t prefix[80];
        swprintf_s(prefix, L"%04u-%02u-%02u %02u:%02u:%02u pid=%lu ", now.wYear, now.wMonth, now.wDay,
                   now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
        std::ofstream stream(data_directory() / (engine ? L"engine.log" : L"controller.log"), std::ios::app);
        stream << utf8(prefix) << utf8(value) << '\n';
    } catch (...) {}
}
bool readable(const void* ptr, std::size_t bytes) noexcept {
    if (!ptr || !bytes) return false;
    auto cursor = reinterpret_cast<std::uintptr_t>(ptr);
    if (cursor > (std::numeric_limits<std::uintptr_t>::max)() - bytes) return false;
    const auto end = cursor + bytes;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        auto prot = info.Protect & 0xff;
        if (prot != PAGE_READONLY && prot != PAGE_READWRITE && prot != PAGE_WRITECOPY &&
            prot != PAGE_EXECUTE_READ && prot != PAGE_EXECUTE_READWRITE && prot != PAGE_EXECUTE_WRITECOPY) return false;
        auto next = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= cursor) return false;
        cursor = next;
    }
    return true;
}
bool writable(void* ptr, std::size_t bytes) noexcept {
    if (!readable(ptr, bytes)) return false;
    auto cursor = reinterpret_cast<std::uintptr_t>(ptr);
    const auto end = cursor + bytes;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info))) return false;
        const auto prot = info.Protect & 0xff;
        if (prot != PAGE_READWRITE && prot != PAGE_WRITECOPY && prot != PAGE_EXECUTE_READWRITE && prot != PAGE_EXECUTE_WRITECOPY) return false;
        cursor = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}

namespace {
template<class T> T read(const std::vector<unsigned char>& data, std::size_t offset) {
    if (offset > data.size() || sizeof(T) > data.size() - offset) throw std::runtime_error("Truncated PE image");
    T value; std::memcpy(&value, data.data() + offset, sizeof(T)); return value;
}
struct CvHeader { DWORD signature; GUID guid; DWORD age; };
constexpr DWORD kRsds = 0x53445352;
}
ModuleFile inspect_module_file(const std::filesystem::path& path) {
    const auto length = std::filesystem::file_size(path);
    if (length < sizeof(IMAGE_DOS_HEADER) || length > 256 * 1024 * 1024) throw std::runtime_error("Invalid PE file size");
    std::vector<unsigned char> data(static_cast<std::size_t>(length));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(length))) throw std::runtime_error("Cannot read PE image");
    const auto dos = read<IMAGE_DOS_HEADER>(data, 0);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) throw std::runtime_error("Not a PE image");
    const auto nt = read<IMAGE_NT_HEADERS64>(data, dos.e_lfanew);
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG ||
        nt.FileHeader.NumberOfSections > 96) throw std::runtime_error("Expected x64 PE image with debug identity");
    auto rva_offset = [&](DWORD rva) -> std::size_t {
        const auto first = static_cast<std::size_t>(dos.e_lfanew) + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
        for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            const auto section = read<IMAGE_SECTION_HEADER>(data, first + i * sizeof(IMAGE_SECTION_HEADER));
            if (rva >= section.VirtualAddress && rva - section.VirtualAddress < section.SizeOfRawData)
                return static_cast<std::size_t>(section.PointerToRawData) + rva - section.VirtualAddress;
        }
        throw std::runtime_error("PE RVA is outside file-backed sections");
    };
    const auto debug = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (!debug.VirtualAddress || debug.Size > 1024 * 1024) throw std::runtime_error("Missing PE debug directory");
    auto debugOffset = rva_offset(debug.VirtualAddress);
    for (std::size_t i = 0; i < debug.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++i) {
        auto entry = read<IMAGE_DEBUG_DIRECTORY>(data, debugOffset + i * sizeof(IMAGE_DEBUG_DIRECTORY));
        if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || entry.SizeOfData < sizeof(CvHeader) + 1) continue;
        auto cv = read<CvHeader>(data, entry.PointerToRawData);
        if (cv.signature != kRsds) continue;
        auto offset = static_cast<std::size_t>(entry.PointerToRawData) + sizeof(CvHeader);
        const auto bytes = static_cast<std::size_t>(entry.SizeOfData) - sizeof(CvHeader);
        if (offset > data.size() || bytes > data.size() - offset) throw std::runtime_error("Invalid CodeView path");
        const char* name = reinterpret_cast<const char*>(data.data() + offset);
        const auto terminator = static_cast<const char*>(std::memchr(name, 0, bytes));
        if (!terminator) throw std::runtime_error("Unterminated CodeView path");
        std::string pdb(name, terminator);
        pdb = pdb.substr(pdb.find_last_of("/\\") == std::string::npos ? 0 : pdb.find_last_of("/\\") + 1);
        if (pdb.empty() || pdb.size() > 200 || pdb.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != std::string::npos || !pdb.ends_with(".pdb"))
            throw std::runtime_error("Invalid PDB filename");
        return {path, {nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage, cv.guid, cv.age, nt.FileHeader.Machine, 0}, std::wstring(pdb.begin(), pdb.end())};
    }
    throw std::runtime_error("No RSDS PDB identity found");
}
ModuleIdentity inspect_loaded_module(HMODULE module) {
    auto base = reinterpret_cast<const unsigned char*>(module);
    if (!readable(base, sizeof(IMAGE_DOS_HEADER))) throw std::runtime_error("Unreadable module");
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 1024 * 1024) throw std::runtime_error("Invalid module DOS header");
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (!readable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG)
        throw std::runtime_error("Invalid loaded x64 image");
    const auto size = nt->OptionalHeader.SizeOfImage;
    auto span_at = [&](DWORD rva, DWORD bytes) -> const unsigned char* {
        if (rva >= size || bytes > size - rva || !readable(base + rva, bytes)) throw std::runtime_error("Invalid loaded image RVA");
        return base + rva;
    };
    auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    auto entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(span_at(dir.VirtualAddress, dir.Size));
    for (std::size_t i = 0; i < dir.Size / sizeof(*entries); ++i) {
        if (entries[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW || entries[i].SizeOfData < sizeof(CvHeader)) continue;
        CvHeader cv{}; std::memcpy(&cv, span_at(entries[i].AddressOfRawData, sizeof(cv)), sizeof(cv));
        if (cv.signature == kRsds) return {nt->FileHeader.TimeDateStamp, size, cv.guid, cv.age, nt->FileHeader.Machine, 0};
    }
    throw std::runtime_error("Loaded module has no RSDS identity");
}
std::wstring pdb_key(const ModuleIdentity& id) {
    wchar_t value[80];
    const auto& g = id.pdbGuid;
    swprintf_s(value, L"%08lX%04hX%04hX%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX%lX", g.Data1, g.Data2, g.Data3,
        g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7], id.pdbAge);
    return value;
}
Environment inspect_environment() {
    Environment result;
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    RTL_OSVERSIONINFOW version{sizeof(version)};
    if (!fn || fn(&version) != 0) throw std::runtime_error("Cannot determine Windows version");
    result.build = version.dwBuildNumber;
    if (result.build < 22000) throw std::runtime_error("This prototype targets the Windows 11 taskbar only");
    USHORT processMachine{}, nativeMachine{};
    if (!IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine) || nativeMachine != IMAGE_FILE_MACHINE_AMD64)
        throw std::runtime_error("Native x64 Windows required; ARM64 and emulated x64 are not supported yet");
    result.taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!result.taskbar || !(result.tid = GetWindowThreadProcessId(result.taskbar, &result.pid)))
        throw std::runtime_error("No taskbar in this desktop/session");
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, result.pid));
    if (!process) throw std::runtime_error(utf8(error_text()));
    wchar_t processPath[32768]{}; DWORD length = ARRAYSIZE(processPath);
    wchar_t windowsPath[MAX_PATH]{}; GetWindowsDirectoryW(windowsPath, ARRAYSIZE(windowsPath));
    if (!QueryFullProcessImageNameW(process.get(), 0, processPath, &length) ||
        _wcsicmp(processPath, (std::filesystem::path(windowsPath) / L"explorer.exe").c_str()) != 0)
        throw std::runtime_error("Taskbar does not belong to the standard Windows Explorer");
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, result.pid));
    if (!snapshot) throw std::runtime_error(utf8(error_text()));
    MODULEENTRY32W entry{sizeof(entry)};
    if (Module32FirstW(snapshot.get(), &entry)) do {
        if (_wcsicmp(entry.szModule, L"taskbar.dll") == 0) result.modules[0] = inspect_module_file(entry.szExePath);
        if (_wcsicmp(entry.szModule, L"Taskbar.View.dll") == 0 ||
            (_wcsicmp(entry.szModule, L"ExplorerExtensions.dll") == 0 && result.modules[1].path.empty()))
            result.modules[1] = inspect_module_file(entry.szExePath);
    } while (Module32NextW(snapshot.get(), &entry));
    if (result.modules[0].path.empty() || result.modules[1].path.empty())
        throw std::runtime_error("Required Windows 11 taskbar modules are not loaded; open the taskbar and retry. Replacement taskbars are not supported.");
    return result;
}
std::wstring describe_environment(const Environment& env) {
    std::wostringstream out;
    out << L"Windows 11 build " << env.build << L", native x64\r\nExplorer PID: " << env.pid << L", taskbar thread: " << env.tid << L"\r\n";
    for (auto& mod : env.modules) out << mod.path.filename().wstring() << L"\r\n  PDB: " << mod.pdb << L" / " << pdb_key(mod.identity) << L"\r\n";
    return out.str();
}
}
