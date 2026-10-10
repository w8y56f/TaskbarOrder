// SPDX-License-Identifier: GPL-3.0-only
// Derived from m417z's Taskbar Thumbnail Reorder 1.1.5.
#include "engine/reorder.hpp"
#include "../../third_party/minhook/src/hde/hde64.h"
#include "common/accessor.hpp"
#include "common/order.hpp"
#include "common/symbol_catalog.hpp"
#include "common/windows.hpp"
#include <MinHook.h>
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <unknwn.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>

namespace tbo::engine {
namespace {
bool initialized{};
std::atomic<bool> enabled{};
std::atomic<DWORD> shellThread{};
HANDLE ownerProcess{};
HANDLE enableEvent{};
Plan installedPlan{};
bool modernMode{};
std::size_t itemsOffset{};
thread_local bool rejectTaskItem{};
thread_local bool captureDpa{};
thread_local HDPA lastDpa{};

void* taskListVtable{};
using GetHoverIndex = int(WINAPI*)(void*);
using GetThumbnailItem = void*(WINAPI*)(void*, int);
using GetThumbnailGroup = void*(WINAPI*)(void*);
using TaskReordered = void(WINAPI*)(void*, void*);
using NumItems = int(WINAPI*)(void*);
using GetButtonGroup = void*(WINAPI*)(void*, void*, int*);
using GroupType = int(WINAPI*)(void*);
using IndexOf = int(WINAPI*)(void*, void*);
using InclusionChanged = HRESULT(WINAPI*)(void*, void*, void*);
using Allowed = bool(WINAPI*)(void*, void*);
using ThumbnailWndProc = LRESULT(WINAPI*)(void*, HWND, UINT, WPARAM, LPARAM);
GetHoverIndex getHoverIndex{};
GetThumbnailItem getThumbnailItem{};
GetThumbnailGroup getThumbnailGroup{};
TaskReordered taskReordered{};
NumItems numItems{};
GetButtonGroup getButtonGroup{};
GroupType groupType{};
IndexOf indexOf{};
InclusionChanged inclusionChanged{};
Allowed allowedOriginal{};
ThumbnailWndProc thumbnailWndProcOriginal{};
decltype(&DPA_GetPtr) dpaOriginal{};

using ModernThumbnailCtorOld = void*(WINAPI*)(void*, void*, void*, void*, void*, void*, void*, bool);
using ModernThumbnailCtorNew = void*(WINAPI*)(void*, void*, void*, void*, void*, void*, bool);
using ModernExtendedClick = HRESULT(WINAPI*)(void*, void*, void*, void*);
using ModernPointerMoved = int(WINAPI*)(void*, void*);
ModernThumbnailCtorOld modernCtorOldOriginal{};
ModernThumbnailCtorNew modernCtorNewOriginal{};
ModernExtendedClick modernClickOriginal{};
ModernPointerMoved modernMovedOriginal{};
bool modernReordering{};
DWORD lastMoveTick{};
struct ThumbnailMapping {
    winrt::weak_ref<winrt::Windows::Foundation::IInspectable> thumbnail;
    void* group{};
    void* item{};
};
std::vector<ThumbnailMapping> thumbnailMappings;

struct DragState { HWND window{}; int index{-1}; bool moved{}; } drag;

void clear_drag() noexcept {
    if (drag.window && GetCapture() == drag.window) ReleaseCapture();
    drag = {};
    drag.index = -1;
}
bool active() {
    if (GetCurrentThreadId() != shellThread || !enabled) return false;
    if (!ownerProcess || WaitForSingleObject(ownerProcess, 0) != WAIT_TIMEOUT ||
        !enableEvent || WaitForSingleObject(enableEvent, 0) != WAIT_OBJECT_0) {
        enabled = false;
        clear_drag();
        return false;
    }
    return true;
}
bool WINAPI allowed_hook(void* self, void* item) {
    return rejectTaskItem ? false : allowedOriginal(self, item);
}
void* WINAPI dpa_hook(HDPA dpa, INT_PTR index) {
    if (captureDpa) lastDpa = dpa;
    return dpaOriginal(dpa, index);
}
struct PointerArray { int count; void** entries; HANDLE heap; int capacity; int grow; };
PointerArray* checked_array(void* raw) {
    auto* array = static_cast<PointerArray*>(raw);
    if (!readable(array, sizeof(*array)) || array->count < 1 || array->count > 4096 ||
        array->capacity < array->count || !writable(array->entries, array->count * sizeof(void*))) return nullptr;
    return array;
}
std::optional<std::size_t> decode_items_offset(const unsigned char* code) {
    if (!readable(code, 16)) return {};
    return task_items_offset(std::span(code, 16));
}
// Report structured instruction roles and offsets, never raw DLL bytes.
// This diagnostic is never used to decide whether hooks may be installed.
std::wstring task_items_diagnostic(const unsigned char* code) {
    static constexpr const wchar_t* registers[]{
        L"AX", L"CX", L"DX", L"BX", L"SP", L"BP", L"SI", L"DI",
        L"R8", L"R9", L"R10", L"R11", L"R12", L"R13", L"R14", L"R15"
    };
    std::wstring result = L"GetNumItems layout profile v2:";
    std::size_t cursor = 0;
    unsigned returns = 0;
    std::size_t forwardBranchTarget = 0;
    for (unsigned index = 0; index < 12 && cursor < 80; ++index) {
        if (!readable(code + cursor, 15)) { result += L" unreadable"; break; }
        // HDE64 predates CET. Recognize ENDBR64 explicitly before decoding.
        if (code[cursor] == 0xf3 && code[cursor + 1] == 0x0f &&
            code[cursor + 2] == 0x1e && code[cursor + 3] == 0xfa) {
            result += L" [at+" + std::to_wstring(cursor) + L" branch-protection prefix]";
            cursor += 4;
            continue;
        }
        hde64s instruction{};
        const auto length = hde64_disasm(code + cursor, &instruction);
        if (!length || instruction.flags & F_ERROR) { result += L" undecodable"; break; }
        result += L" [at+" + std::to_wstring(cursor) + L" ";
        const bool hasOperand = instruction.flags & F_MODRM;
        const unsigned source = instruction.modrm_rm + (instruction.rex_b ? 8 : 0);
        const unsigned target = instruction.modrm_reg + (instruction.rex_r ? 8 : 0);
        const auto displacement = (instruction.flags & F_DISP8)
            ? static_cast<std::int64_t>(static_cast<std::int8_t>(instruction.disp.disp8))
            : (instruction.flags & F_DISP32)
                ? static_cast<std::int64_t>(static_cast<std::int32_t>(instruction.disp.disp32)) : 0;
        const auto memory = [&] {
            std::wstring value = L"[";
            if (instruction.flags & F_SIB) {
                if (instruction.modrm_mod == 0 && instruction.sib_base == 5) value += L"absolute";
                else value += registers[instruction.sib_base + (instruction.rex_b ? 8 : 0)];
                if (instruction.sib_index != 4 || instruction.rex_x) {
                    value += L"+";
                    value += registers[instruction.sib_index + (instruction.rex_x ? 8 : 0)];
                    value += L"*" + std::to_wstring(1u << instruction.sib_scale);
                }
            } else if (instruction.modrm_mod == 0 && instruction.modrm_rm == 5) {
                value += L"instruction-relative";
            } else {
                value += registers[source];
            }
            value += L" offset=" + std::to_wstring(displacement) + L"]";
            return value;
        };
        if (instruction.opcode == 0x8b && hasOperand) {
            result += instruction.rex_w ? L"load64 " : L"load32 ";
            result += registers[target];
            result += L" <- ";
            result += instruction.modrm_mod == 3 ? registers[source] : memory();
        } else if (instruction.opcode == 0x89 && hasOperand) {
            result += instruction.rex_w ? L"store64 " : L"store32 ";
            result += instruction.modrm_mod == 3 ? registers[source] : memory();
            result += L" <- ";
            result += registers[target];
        } else if (instruction.opcode == 0x8d && hasOperand) {
            result += L"address ";
            result += registers[target];
            result += L" <- ";
            result += memory();
        } else if (instruction.opcode == 0x85 && hasOperand) {
            result += L"test ";
            result += registers[target];
            result += L" with ";
            result += instruction.modrm_mod == 3 ? registers[source] : memory();
        } else if ((instruction.opcode == 0x83 || instruction.opcode == 0x81) && hasOperand) {
            result += instruction.modrm_reg == 7 ? L"compare " :
                      instruction.modrm_reg == 0 ? L"add " :
                      instruction.modrm_reg == 5 ? L"subtract " : L"arithmetic ";
            result += instruction.modrm_mod == 3 ? registers[source] : memory();
            if (instruction.flags & F_IMM8) result += L" constant=" + std::to_wstring(static_cast<std::int8_t>(instruction.imm.imm8));
            else if (instruction.flags & F_IMM32) result += L" constant=" + std::to_wstring(static_cast<std::int32_t>(instruction.imm.imm32));
        } else if (instruction.opcode == 0xc3 || instruction.opcode == 0xc2) {
            result += L"return";
        } else if (instruction.opcode == 0xe9 || instruction.opcode == 0xeb ||
                   (instruction.opcode == 0xff && instruction.flags & F_MODRM && instruction.modrm_reg == 4)) {
            result += L"jump";
        } else if (instruction.opcode == 0xe8) {
            result += L"call";
        } else if (instruction.opcode == 0xa9) {
            result += L"test";
        } else if (instruction.opcode == 0x74 || instruction.opcode == 0x75 ||
                   (instruction.opcode == 0x0f && instruction.opcode2 >= 0x80 && instruction.opcode2 <= 0x8f)) {
            result += L"conditional branch";
            const auto distance = (instruction.flags & F_IMM8)
                ? static_cast<std::int64_t>(static_cast<std::int8_t>(instruction.imm.imm8))
                : static_cast<std::int64_t>(static_cast<std::int32_t>(instruction.imm.imm32));
            const auto destination = static_cast<std::int64_t>(cursor + length) + distance;
            result += L" to at+" + std::to_wstring(destination);
            if (destination > 0 && destination < 80)
                forwardBranchTarget = (std::max)(forwardBranchTarget, static_cast<std::size_t>(destination));
        } else if (instruction.opcode == 0x90 || instruction.opcode == 0xcc) {
            result += L"padding";
        } else {
            result += L"other instruction length=" + std::to_wstring(length);
        }
        result += L"]";
        cursor += length;
        if (instruction.opcode == 0xc3 || instruction.opcode == 0xc2) {
            if (++returns == 2 || cursor >= forwardBranchTarget) break;
        }
        if (instruction.opcode == 0xe9 || instruction.opcode == 0xeb) break;
    }
    return result;
}
void* task_list_interface(void* object) {
    auto slots = static_cast<void**>(object);
    for (std::size_t i = 0; i < 32; ++i) {
        if (!readable(slots + i, sizeof(void*))) return nullptr;
        if (slots[i] == taskListVtable) return slots + i;
    }
    return nullptr;
}
struct ViewMove { PointerArray* array{}; std::size_t from{}, to{}; void* ui{}; HWND list{}; };
struct MoveContext { void* group{}; void* from{}; void* to{}; std::vector<ViewMove> views; bool failed{}; };
BOOL CALLBACK collect_views(HWND window, LPARAM parameter) {
    auto& ctx = *reinterpret_cast<MoveContext*>(parameter);
    try {
        wchar_t name[64]{};
        GetClassNameW(window, name, ARRAYSIZE(name));
        const bool primary = _wcsicmp(name, L"Shell_TrayWnd") == 0;
        if (!primary && _wcsicmp(name, L"Shell_SecondaryTrayWnd") != 0) return TRUE;
        HWND band = primary ? static_cast<HWND>(GetPropW(window, L"TaskbandHWND"))
                            : FindWindowExW(window, nullptr, L"WorkerW", nullptr);
        HWND list = band ? FindWindowExW(band, nullptr, L"MSTaskListWClass", nullptr) : nullptr;
        if (!list) { if (primary) ctx.failed = true; return TRUE; }
        void* object = reinterpret_cast<void*>(GetWindowLongPtrW(list, 0));
        if (!object) { ctx.failed = true; return TRUE; }
        auto buttonGroup = getButtonGroup(object, ctx.group, nullptr);
        if (!buttonGroup) return TRUE;
        const int type = groupType(buttonGroup);
        if (type != 1 && type != 3) { ctx.failed = true; return TRUE; }
        lastDpa = nullptr; captureDpa = true;
        const int from = indexOf(buttonGroup, ctx.from);
        captureDpa = false;
        auto array = checked_array(lastDpa);
        lastDpa = nullptr; captureDpa = true;
        const int to = indexOf(buttonGroup, ctx.to);
        captureDpa = false;
        auto ui = task_list_interface(object);
        if (!array || reinterpret_cast<void*>(lastDpa) != array || !ui ||
            from < 0 || to < 0 || from >= array->count || to >= array->count) {
            ctx.failed = true;
            return TRUE;
        }
        ctx.views.push_back({array, static_cast<std::size_t>(from), static_cast<std::size_t>(to), ui, list});
        return TRUE;
    } catch (...) {
        captureDpa = false;
        ctx.failed = true;
        return FALSE;
    }
}

bool move_group(void* group, void* itemFrom, void* itemTo) {
    if (!group || !itemFrom || !itemTo || itemFrom == itemTo) return false;
    const auto slot = static_cast<unsigned char*>(group) + itemsOffset;
    if (!readable(slot, sizeof(void*))) return false;
    void* raw{};
    std::memcpy(&raw, slot, sizeof(raw));
    auto model = checked_array(raw);
    if (!model || numItems(group) != model->count) return false;
    std::span<void*> items(model->entries, static_cast<std::size_t>(model->count));
    const auto from = std::find(items.begin(), items.end(), itemFrom);
    const auto to = std::find(items.begin(), items.end(), itemTo);
    if (from == items.end() || to == items.end()) return false;
    const auto fromIndex = static_cast<std::size_t>(from - items.begin());
    const auto toIndex = static_cast<std::size_t>(to - items.begin());
    MoveContext context{group, itemFrom, itemTo};
    EnumThreadWindows(GetCurrentThreadId(), collect_views, reinterpret_cast<LPARAM>(&context));
    if (context.failed || context.views.empty()) return false;
    for (const auto& view : context.views) if (view.array == model) return false;
    move_to<void*>(items, fromIndex, toIndex);
    for (std::size_t i = 0; i < context.views.size(); ++i) {
        auto& view = context.views[i];
        bool duplicate{};
        for (std::size_t j = 0; j < i; ++j) if (context.views[j].array == view.array) duplicate = true;
        if (!duplicate) move_to<void*>(std::span<void*>(view.array->entries, view.array->count), view.from, view.to);
    }
    for (const auto& view : context.views) {
        rejectTaskItem = true;
        inclusionChanged(view.ui, group, itemTo);
        rejectTaskItem = false;
        inclusionChanged(view.ui, group, itemTo);
        InvalidateRect(view.list, nullptr, FALSE);
    }
    log(L"Reordered one hover-preview thumbnail", true);
    return true;
}

bool move_from_thumbnail(void* thumbnailObject, int from, int to) {
    if (from < 0 || to < 0 || from == to) return false;
    auto* itemFrom = getThumbnailItem(thumbnailObject, from);
    auto* itemTo = getThumbnailItem(thumbnailObject, to);
    auto* group = getThumbnailGroup(thumbnailObject);
    if (!itemFrom || !itemTo || !group || !move_group(group, itemFrom, itemTo)) return false;
    taskReordered(thumbnailObject, itemFrom);
    return true;
}
LRESULT WINAPI thumbnail_wndproc_hook(void* self, HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto original = [&] { return thumbnailWndProcOriginal(self, window, message, wp, lp); };
    if (!active()) return original();
    try {
        switch (message) {
        case WM_LBUTTONDOWN: {
            const auto result = original();
            auto* object = reinterpret_cast<void*>(GetWindowLongPtrW(window, 0));
            const int hover = object ? getHoverIndex(object) : -1;
            if (hover >= 0) {
                drag = {window, hover, false};
                SetCapture(window);
            }
            return result;
        }
        case WM_MOUSEMOVE: {
            const auto result = original();
            if (drag.window == window && GetCapture() == window && (wp & MK_LBUTTON)) {
                auto* object = reinterpret_cast<void*>(GetWindowLongPtrW(window, 0));
                const int hover = object ? getHoverIndex(object) : -1;
                if (hover >= 0 && hover != drag.index && move_from_thumbnail(object, drag.index, hover)) {
                    drag.index = hover;
                    drag.moved = true;
                }
            }
            return result;
        }
        case WM_LBUTTONUP:
            if (drag.window == window && GetCapture() == window) {
                const bool moved = drag.moved;
                clear_drag();
                if (moved) return DefWindowProcW(window, message, wp, lp);
            }
            return original();
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            if (drag.window == window) clear_drag();
            return original();
        case WM_TIMER:
            if (wp == 2006 && drag.window == window && GetCapture() == window) {
                KillTimer(window, wp);
                return DefWindowProcW(window, message, wp, lp);
            }
            return original();
        default:
            return original();
        }
    } catch (...) {
        clear_drag();
        log(L"Thumbnail drag canceled after an internal error", true);
        return original();
    }
}

void add_thumbnail_mapping(winrt::Windows::Foundation::IInspectable object,
                           void* group,
                           void* item) {
    std::erase_if(thumbnailMappings, [&](const ThumbnailMapping& mapping) {
        auto current = mapping.thumbnail.get();
        return !current || current == object ||
            (mapping.group == group && mapping.item == item);
    });
    thumbnailMappings.push_back({object, group, item});
}
void capture_constructed_thumbnail(void* result, void* group, void* item) {
    if (!result || !group || !item) return;
    winrt::Windows::Foundation::IInspectable object{nullptr};
    auto identity = reinterpret_cast<IUnknown*>(
        static_cast<unsigned char*>(result) + 2 * sizeof(void*));
    if (SUCCEEDED(identity->QueryInterface(
            winrt::guid_of<winrt::Windows::Foundation::IInspectable>(),
            winrt::put_abi(object))) && object) {
        add_thumbnail_mapping(object, group, item);
    }
}
void* WINAPI modern_ctor_old_hook(void* p1, void* p2, void* group, void* item,
                                  void* p5, void* p6, void* p7, bool p8) {
    auto result = modernCtorOldOriginal(p1, p2, group, item, p5, p6, p7, p8);
    capture_constructed_thumbnail(result, group, item);
    return result;
}
void* WINAPI modern_ctor_new_hook(void* p1, void* p2, void* group, void* item,
                                  void* p5, void* p6, bool p7) {
    auto result = modernCtorNewOriginal(p1, p2, group, item, p5, p6, p7);
    capture_constructed_thumbnail(result, group, item);
    return result;
}
HRESULT WINAPI modern_click_hook(void* self, void* group, void* item, void* options) {
    if (modernReordering) {
        modernReordering = false;
        return S_OK;
    }
    return modernClickOriginal(self, group, item, options);
}

using FrameworkElement = winrt::Windows::UI::Xaml::FrameworkElement;
using DependencyObject = winrt::Windows::UI::Xaml::DependencyObject;
using PointerArgs = winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs;
using VisualTree = winrt::Windows::UI::Xaml::Media::VisualTreeHelper;

void collect_thumbnail_views(const DependencyObject& root,
                             std::vector<FrameworkElement>& output,
                             unsigned depth = 0) {
    if (!root || depth > 24 || output.size() >= 256) return;
    int count = VisualTree::GetChildrenCount(root);
    if (count < 0 || count > 512) return;
    for (int i = 0; i < count && output.size() < 256; ++i) {
        auto child = VisualTree::GetChild(root, i);
        if (!child) continue;
        if (winrt::get_class_name(child) == L"Taskbar.TaskItemThumbnailView") {
            if (auto view = child.try_as<FrameworkElement>()) output.push_back(view);
        }
        collect_thumbnail_views(child, output, depth + 1);
    }
}
bool pointer_inside(const FrameworkElement& element, const PointerArgs& args) {
    const auto point = args.GetCurrentPoint(element).Position();
    return point.X >= 0 && point.Y >= 0 &&
        point.X < element.ActualWidth() && point.Y < element.ActualHeight();
}
bool view_pressed(const FrameworkElement& view) {
    int count = VisualTree::GetChildrenCount(view);
    for (int i = 0; i < count; ++i) {
        auto child = VisualTree::GetChild(view, i);
        auto grid = child.try_as<winrt::Windows::UI::Xaml::Controls::Grid>();
        if (!grid) continue;
        for (const auto& group : winrt::Windows::UI::Xaml::VisualStateManager::GetVisualStateGroups(grid)) {
            if (group.Name() != L"CommonStates" || !group.CurrentState()) continue;
            const auto state = group.CurrentState().Name();
            return state == L"Pressed" || state == L"RequestingAttentionPressed";
        }
    }
    return false;
}
std::optional<ThumbnailMapping> mapping_for_object(
        const winrt::Windows::Foundation::IInspectable& object) {
    if (!object) return {};
    void* abi = winrt::get_abi(object);
    for (auto it = thumbnailMappings.begin(); it != thumbnailMappings.end();) {
        auto current = it->thumbnail.get();
        if (!current) {
            it = thumbnailMappings.erase(it);
            continue;
        }
        if (winrt::get_abi(current) == abi) return *it;
        ++it;
    }
    return {};
}
std::optional<ThumbnailMapping> mapping_for_view(const FrameworkElement& view) {
    if (auto mapping = mapping_for_object(view.DataContext())) return mapping;
    // The exact template differs between 25H2 servicing builds. Search a small
    // bounded subtree for the element whose DataContext is TaskItemThumbnail.
    std::vector<DependencyObject> pending{view};
    for (std::size_t cursor = 0; cursor < pending.size() && pending.size() < 64; ++cursor) {
        if (!pending[cursor]) continue;
        auto element = pending[cursor].try_as<FrameworkElement>();
        if (element) if (auto mapping = mapping_for_object(element.DataContext())) return mapping;
        int count = VisualTree::GetChildrenCount(pending[cursor]);
        if (count < 0 || count > 64) continue;
        for (int i = 0; i < count && pending.size() < 64; ++i)
            pending.push_back(VisualTree::GetChild(pending[cursor], i));
    }
    return {};
}
int WINAPI modern_moved_hook(void* self, void* rawArgs) {
    auto original = [&] { return modernMovedOriginal(self, rawArgs); };
    if (!active()) return original();
    try {
        if (!self || !rawArgs) return original();
        if (!GetCapture()) {
            modernReordering = false;
            return original();
        }
        if (GetTickCount() - lastMoveTick <= 60) return original();
        FrameworkElement list{nullptr};
        static_cast<IUnknown*>(self)->QueryInterface(winrt::guid_of<FrameworkElement>(),
                                                     winrt::put_abi(list));
        PointerArgs args{nullptr};
        static_cast<IUnknown*>(rawArgs)->QueryInterface(winrt::guid_of<PointerArgs>(),
                                                        winrt::put_abi(args));
        if (!list || !args) return original();
        const auto className = winrt::get_class_name(list);
        if (className != L"Taskbar.TaskItemThumbnailList" &&
            className != L"Taskbar.TaskItemThumbnailScrollableList") return original();
        std::vector<FrameworkElement> views;
        collect_thumbnail_views(list, views);
        std::optional<ThumbnailMapping> source, target;
        for (const auto& view : views) {
            if (!source && view_pressed(view)) source = mapping_for_view(view);
            if (!target && pointer_inside(view, args)) target = mapping_for_view(view);
            if (source && target) break;
        }
        if (source && target && source->group == target->group &&
            source->item != target->item && move_group(source->group, source->item, target->item)) {
            modernReordering = true;
            lastMoveTick = GetTickCount();
        }
    } catch (...) {
        modernReordering = false;
        log(L"Modern thumbnail drag canceled after an internal error", true);
    }
    return original();
}

void check_mh(MH_STATUS status) {
    if (status != MH_OK) throw std::runtime_error(std::string("MinHook: ") + MH_StatusToString(status));
}
template<class T> void hook(void* target, T callback, T& original) {
    check_mh(MH_CreateHook(target, reinterpret_cast<void*>(callback), reinterpret_cast<void**>(&original)));
}
void initialize(const Plan& plan) {
    HMODULE modules[]{GetModuleHandleW(L"taskbar.dll"), GetModuleHandleW(L"Taskbar.View.dll")};
    if (!modules[1]) modules[1] = GetModuleHandleW(L"ExplorerExtensions.dll");
    std::array<void*, kSymbolCount> symbols{};
    for (unsigned i = 0; i < 2; ++i) {
        if (!modules[i] || !same_identity(inspect_loaded_module(modules[i]), plan.modules[i]))
            throw std::runtime_error("Loaded taskbar module does not match the prepared symbols");
    }
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        const auto module = kSymbols[i].module;
        const auto rva = plan.rvas[i];
        if (!rva) continue;
        if (rva >= plan.modules[module].imageSize) throw std::runtime_error("Invalid symbol address");
        symbols[i] = reinterpret_cast<unsigned char*>(modules[module]) + rva;
        MEMORY_BASIC_INFORMATION info{};
        if (!readable(symbols[i], sizeof(void*)) || !VirtualQuery(symbols[i], &info, sizeof(info)) ||
            info.AllocationBase != modules[module]) throw std::runtime_error("Symbol address outside module");
        if (!kSymbols[i].data && !(info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            throw std::runtime_error("Function symbol is not executable");
    }
    auto offset = decode_items_offset(static_cast<const unsigned char*>(symbols[SymGroupNumItems]));
    if (!offset) {
        log(task_items_diagnostic(static_cast<const unsigned char*>(symbols[SymGroupNumItems])), true);
        throw std::runtime_error("Unsupported task item layout; no hooks installed. See engine.log for the layout profile");
    }
    itemsOffset = *offset;
    taskListVtable = symbols[SymTaskListVtable];
    numItems = reinterpret_cast<NumItems>(symbols[SymGroupNumItems]);
    getButtonGroup = reinterpret_cast<GetButtonGroup>(symbols[SymGetButtonGroup]);
    groupType = reinterpret_cast<GroupType>(symbols[SymButtonGroupType]);
    indexOf = reinterpret_cast<IndexOf>(symbols[SymButtonIndexOf]);
    inclusionChanged = reinterpret_cast<InclusionChanged>(symbols[SymTaskInclusionChanged]);
    modernMode = supports_modern(plan);
    check_mh(MH_Initialize());
    try {
        hook(symbols[SymTaskAllowed], allowed_hook, allowedOriginal);
        if (modernMode) {
            if (symbols[SymModernThumbnailCtorOld])
                hook(symbols[SymModernThumbnailCtorOld], modern_ctor_old_hook, modernCtorOldOriginal);
            if (symbols[SymModernThumbnailCtorNew])
                hook(symbols[SymModernThumbnailCtorNew], modern_ctor_new_hook, modernCtorNewOriginal);
            hook(symbols[SymModernExtendedClick], modern_click_hook, modernClickOriginal);
            hook(symbols[SymModernPointerMoved], modern_moved_hook, modernMovedOriginal);
        } else {
            getHoverIndex = reinterpret_cast<GetHoverIndex>(symbols[SymClassicHoverIndex]);
            getThumbnailItem = reinterpret_cast<GetThumbnailItem>(symbols[SymClassicGetItem]);
            getThumbnailGroup = reinterpret_cast<GetThumbnailGroup>(symbols[SymClassicGetGroup]);
            taskReordered = reinterpret_cast<TaskReordered>(symbols[SymClassicTaskReordered]);
            hook(symbols[SymClassicWndProc], thumbnail_wndproc_hook, thumbnailWndProcOriginal);
        }
        hook(reinterpret_cast<void*>(&DPA_GetPtr), dpa_hook, dpaOriginal);
        check_mh(MH_EnableHook(MH_ALL_HOOKS));
    } catch (...) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        throw;
    }
    installedPlan = plan;
    initialized = true;
}
}

bool start(const Plan& plan, DWORD controllerPid, std::wstring& message) {
    if (initialized) {
        for (unsigned i = 0; i < 2; ++i) if (!same_identity(installedPlan.modules[i], plan.modules[i]))
            throw std::runtime_error("Resident engine uses different modules; restart Explorer before re-enabling");
        if (installedPlan.rvas != plan.rvas) throw std::runtime_error("Resident symbol plan differs; restart Explorer");
    }
    HANDLE owner = OpenProcess(SYNCHRONIZE, FALSE, controllerPid);
    if (!owner || WaitForSingleObject(owner, 0) != WAIT_TIMEOUT) {
        if (owner) CloseHandle(owner);
        throw std::runtime_error("Controller process is no longer alive");
    }
    HANDLE gate = OpenEventW(SYNCHRONIZE, FALSE, enable_event_name(controllerPid).c_str());
    if (!gate || WaitForSingleObject(gate, 0) != WAIT_OBJECT_0) {
        CloseHandle(owner); if (gate) CloseHandle(gate);
        throw std::runtime_error("Controller canceled activation");
    }
    try { if (!initialized) initialize(plan); }
    catch (...) { CloseHandle(owner); CloseHandle(gate); throw; }
    if (ownerProcess) CloseHandle(ownerProcess);
    if (enableEvent) CloseHandle(enableEvent);
    ownerProcess = owner;
    enableEvent = gate;
    shellThread = GetCurrentThreadId();
    clear_drag();
    enabled = true;
    message = modernMode
        ? L"已启用 25H2/XAML 预览支持：在弹出的预览中按住左键拖动卡片调序。"
        : L"已启用经典预览支持：在弹出的预览中按住左键拖动卡片调序。";
    return true;
}
void stop() noexcept {
    enabled = false;
    try { clear_drag(); } catch (...) {}
    if (ownerProcess) { CloseHandle(ownerProcess); ownerProcess = nullptr; }
    if (enableEvent) { CloseHandle(enableEvent); enableEvent = nullptr; }
}
}
