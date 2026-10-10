// SPDX-License-Identifier: GPL-3.0-only
#include "controller/symbols.hpp"
#include "common/symbol_catalog.hpp"
#include <dbghelp.h>
#include <winhttp.h>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cwctype>

namespace tbo {
namespace {
struct InternetHandle {
    HINTERNET h{};
    explicit InternetHandle(HINTERNET value) : h(value) {}
    ~InternetHandle() { if (h) WinHttpCloseHandle(h); }
    InternetHandle(const InternetHandle&) = delete;
};
std::filesystem::path plan_path(const Environment& env) {
    return data_directory() / (L"plan-" + pdb_key(env.modules[0].identity) + L"-" + pdb_key(env.modules[1].identity) + L".bin");
}
void download(const std::wstring& remotePath, const std::filesystem::path& destination,
              const std::function<bool()>& canceled) {
    InternetHandle session(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) throw std::runtime_error(utf8(error_text()));
    WinHttpSetTimeouts(session.h, 15000, 15000, 15000, 30000);
    InternetHandle connect(WinHttpConnect(session.h, L"msdl.microsoft.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connect.h) throw std::runtime_error(utf8(error_text()));
    InternetHandle request(WinHttpOpenRequest(connect.h, L"GET", remotePath.c_str(), nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.h) throw std::runtime_error(utf8(error_text()));
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) throw std::runtime_error(utf8(error_text()));
    DWORD status{}, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200)
        throw std::runtime_error("Microsoft symbol download failed, HTTP " + std::to_string(status) + ". An offline PDB can be placed in the symbol cache.");
    auto temporary = destination; temporary += L".partial";
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot create symbol cache file");
        std::vector<char> buffer(64 * 1024);
        std::uint64_t total = 0;
        const auto deadline = GetTickCount64() + 5 * 60 * 1000;
        for (;;) {
            if (canceled()) throw std::runtime_error("Canceled");
            DWORD received{};
            if (!WinHttpReadData(request.h, buffer.data(), static_cast<DWORD>(buffer.size()), &received)) throw std::runtime_error(utf8(error_text()));
            if (!received) break;
            total += received;
            if (total > 512 * 1024 * 1024 || GetTickCount64() > deadline) throw std::runtime_error("Symbol download exceeded size/time limit");
            if (!out.write(buffer.data(), received)) throw std::runtime_error("Cannot write downloaded symbols");
        }
        out.close();
        if (!out || total < 32) throw std::runtime_error("Incomplete symbol download");
        std::filesystem::rename(temporary, destination);
    } catch (...) {
        std::error_code ignored; std::filesystem::remove(temporary, ignored); throw;
    }
}
// DbgHelp is used only by the controller's single preparation worker, never
// inside Explorer. Do not enable SYMOPT_LOAD_ANYTHING: mismatched PDBs must fail.
struct SymbolSession {
    HANDLE key{reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(0x54424F))};
    explicit SymbolSession(const std::filesystem::path& cache) {
        SymSetOptions(SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_EXACT_SYMBOLS | SYMOPT_NO_PROMPTS | SYMOPT_IGNORE_NT_SYMPATH);
        if (!SymInitializeW(key, cache.c_str(), FALSE)) throw std::runtime_error(utf8(error_text()));
    }
    ~SymbolSession() { SymCleanup(key); }
};
std::wstring canonical(std::wstring_view input) {
    std::wstring value(input);
    for (const auto* token : {L" __ptr64", L"__ptr64 "}) {
        for (auto pos = value.find(token); pos != std::wstring::npos; pos = value.find(token))
            value.erase(pos, std::wcslen(token));
    }
    std::wstring result;
    for (auto ch : value) if (!std::iswspace(ch)) result.push_back(ch);
    return result;
}
struct ResolveContext {
    unsigned module{};
    DWORD64 base{};
    DWORD imageSize{};
    Plan* plan{};
    std::array<unsigned, kSymbolCount> matches{};
    bool failed{};
};
BOOL CALLBACK enumerate(PSYMBOL_INFOW info, ULONG, void* parameter) {
    auto& context = *static_cast<ResolveContext*>(parameter);
    try {
        wchar_t undecorated[8192];
        std::wstring name(info->Name, info->NameLen);
        if (UnDecorateSymbolNameW(name.c_str(), undecorated, ARRAYSIZE(undecorated), UNDNAME_COMPLETE)) name = undecorated;
        name = canonical(name);
        for (std::size_t i = 0; i < kSymbols.size(); ++i) {
            if (kSymbols[i].module != context.module ||
                (name != canonical(kSymbols[i].name) &&
                 (kSymbols[i].alternate.empty() || name != canonical(kSymbols[i].alternate)))) continue;
            if (info->Address <= context.base || info->Address - context.base >= context.imageSize) {
                context.failed = true; return FALSE;
            }
            const auto rva = static_cast<DWORD>(info->Address - context.base);
            if (context.matches[i] && context.plan->rvas[i] != rva) {
                context.failed = true; return FALSE;
            }
            context.plan->rvas[i] = rva;
            ++context.matches[i];
        }
        return TRUE;
    } catch (...) { context.failed = true; return FALSE; }
}
}

Plan prepare_symbols(const Environment& env, bool allowDownload, const Progress& progress,
                     const std::function<bool()>& canceled) {
    Plan result;
    for (unsigned m = 0; m < 2; ++m) {
        if (canceled()) throw std::runtime_error("Canceled");
        const auto& module = env.modules[m];
        result.modules[m] = module.identity;
        bool moduleHasSymbols = false;
        for (const auto& symbol : kSymbols) if (symbol.module == m) moduleHasSymbols = true;
        if (!moduleHasSymbols) continue;
        const auto cache = data_directory() / L"symbols" / module.pdb / pdb_key(module.identity);
        std::filesystem::create_directories(cache);
        const auto pdbPath = cache / module.pdb;
        if (!std::filesystem::exists(pdbPath)) {
            if (!allowDownload) throw std::runtime_error("Missing cached symbols: " + utf8(pdbPath.wstring()));
            progress(L"正在从 Microsoft 下载 " + module.pdb + L" …");
            download(L"/download/symbols/" + module.pdb + L"/" + pdb_key(module.identity) + L"/" + module.pdb, pdbPath, canceled);
        }
        progress(L"正在匹配 " + module.path.filename().wstring() + L" 的内部函数 …");
        SymbolSession session(cache);
        constexpr DWORD64 base = 0x180000000ULL;
        const auto loaded = SymLoadModuleExW(session.key, nullptr, module.path.c_str(), nullptr, base, module.identity.imageSize, nullptr, 0);
        if (!loaded) throw std::runtime_error("Cannot load matching PDB: " + utf8(error_text()));
        IMAGEHLP_MODULEW64 details{sizeof(details)};
        if (!SymGetModuleInfoW64(session.key, loaded, &details) || details.PdbUnmatched || details.DbgUnmatched ||
            !InlineIsEqualGUID(details.PdbSig70, module.identity.pdbGuid) || details.PdbAge != module.identity.pdbAge)
            throw std::runtime_error("PDB identity mismatch; no taskbar hooks were installed");
        ResolveContext context{m, loaded, module.identity.imageSize, &result};
        if (!SymEnumSymbolsW(session.key, loaded, nullptr, enumerate, &context) || context.failed)
            throw std::runtime_error("Symbol enumeration failed or names are ambiguous");
        for (std::size_t i = 0; i < kSymbols.size(); ++i) {
            if (kSymbols[i].module == m && !kSymbols[i].optional && !context.matches[i]) {
                log(L"Missing symbol: " + std::wstring(kSymbols[i].name));
                throw std::runtime_error("Unsupported taskbar internals; missing: " + utf8(kSymbols[i].name));
            }
        }
    }
    if (!supports_any_thumbnail(result))
        throw std::runtime_error("This Windows build's hover-preview implementation is not supported by this prototype");
    save_plan(env, result);
    return result;
}
void save_plan(const Environment& env, const Plan& plan) {
    auto path = plan_path(env), temporary = path; temporary += L".partial";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    const std::uint32_t magic = kMagic;
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&plan), sizeof(plan));
    file.close();
    if (!file) throw std::runtime_error("Cannot save symbol plan");
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error(utf8(error_text()));
}
Plan load_plan(const Environment& env) {
    Plan plan{}; std::uint32_t magic{};
    std::ifstream file(plan_path(env), std::ios::binary);
    if (!file.read(reinterpret_cast<char*>(&magic), sizeof(magic)) || magic != kMagic ||
        !file.read(reinterpret_cast<char*>(&plan), sizeof(plan)) || file.peek() != EOF)
        throw std::runtime_error("No prepared symbol plan for these taskbar modules; click Prepare symbols first");
    for (std::size_t m = 0; m < 2; ++m) if (!same_identity(plan.modules[m], env.modules[m].identity))
        throw std::runtime_error("Taskbar module identity changed; prepare symbols again");
    for (std::size_t i = 0; i < kSymbols.size(); ++i)
        if ((!kSymbols[i].optional && !plan.rvas[i]) ||
            (plan.rvas[i] && plan.rvas[i] >= plan.modules[kSymbols[i].module].imageSize))
            throw std::runtime_error("Invalid cached symbol RVA");
    if (!supports_any_thumbnail(plan))
        throw std::runtime_error("Cached plan has no supported hover-preview implementation");
    return plan;
}
}
