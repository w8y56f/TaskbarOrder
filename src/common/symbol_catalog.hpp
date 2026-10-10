// SPDX-License-Identifier: GPL-3.0-only
// Symbol declarations derived from m417z's Taskbar Thumbnail Reorder mod.
#pragma once
#include "common/protocol.hpp"
#include <array>
#include <string_view>

namespace tbo {
enum SymbolIndex : std::size_t {
    SymTaskListVtable,
    SymGroupNumItems,
    SymGetButtonGroup,
    SymButtonGroupType,
    SymButtonIndexOf,
    SymTaskInclusionChanged,
    SymTaskAllowed,
    SymClassicHoverIndex,
    SymClassicGetItem,
    SymClassicGetGroup,
    SymClassicTaskReordered,
    SymClassicWndProc,
    SymModernThumbnailCtorOld,
    SymModernThumbnailCtorNew,
    SymModernExtendedClick,
    SymModernPointerMoved,
};

struct SymbolSpec {
    unsigned module;
    std::wstring_view name;
    std::wstring_view alternate;
    bool data;
    bool optional;
};

inline constexpr std::array<SymbolSpec, kSymbolCount> kSymbols{{
    {0, LR"(const CTaskListWnd::`vftable'{for `ITaskListUI'})", {}, true, false},
    {0, LR"(public: virtual int __cdecl CTaskGroup::GetNumItems(void))", {}, false, false},
    {0, LR"(protected: struct ITaskBtnGroup * __cdecl CTaskListWnd::_GetTBGroupFromGroup(struct ITaskGroup *,int *))", {}, false, false},
    {0, LR"(public: virtual enum eTBGROUPTYPE __cdecl CTaskBtnGroup::GetGroupType(void))", {}, false, false},
    {0, LR"(public: virtual int __cdecl CTaskBtnGroup::IndexOfTaskItem(struct ITaskItem *))", {}, false, false},
    {0, LR"(public: virtual long __cdecl CTaskListWnd::TaskInclusionChanged(struct ITaskGroup *,struct ITaskItem *))", {}, false, false},
    {0, LR"(public: virtual bool __cdecl TaskItemFilter::IsTaskAllowed(struct ITaskItem *))", {}, false, false},

    {0, LR"(public: virtual int __cdecl CTaskListThumbnailWnd::GetHoverIndex(void)const )", {}, false, true},
    {0, LR"(private: struct ITaskItem * __cdecl CTaskListThumbnailWnd::_GetTaskItem(int)const )",
        LR"(private: struct ITaskItem * __cdecl CTaskListThumbnailWnd::_GetTaskItem(int))", false, true},
    {0, LR"(public: virtual struct ITaskGroup * __cdecl CTaskListThumbnailWnd::GetTaskGroup(void)const )", {}, false, true},
    {0, LR"(public: virtual void __cdecl CTaskListThumbnailWnd::TaskReordered(struct ITaskItem *))", {}, false, true},
    {0, LR"(private: virtual __int64 __cdecl CTaskListThumbnailWnd::v_WndProc(struct HWND__ *,unsigned int,unsigned __int64,__int64))", {}, false, true},

    {0, LR"(public: __cdecl winrt::WindowsUdk::UI::Shell::implementation::TaskItemThumbnail::TaskItemThumbnail(struct winrt::WindowsUdk::UI::Shell::TaskItem const &,struct ITaskGroup *,struct ITaskItem *,struct ITaskListUI *,struct IWICImagingFactory *,struct ITaskListAcc *,bool))", {}, false, true},
    {0, LR"(public: __cdecl winrt::WindowsUdk::UI::Shell::implementation::TaskItemThumbnail::TaskItemThumbnail(struct winrt::WindowsUdk::UI::Shell::TaskItem const &,struct ITaskGroup *,struct ITaskItem *,struct ITaskListUI *,struct IWICImagingFactory *,bool))", {}, false, true},
    {0, LR"(public: virtual long __cdecl CTaskListWnd::HandleExtendedUIClick(struct ITaskGroup *,struct ITaskItem *,struct winrt::Windows::System::LauncherOptions const &))", {}, false, true},
    {1, LR"(public: virtual int __cdecl winrt::impl::produce<struct winrt::Taskbar::implementation::TaskItemThumbnailList,struct winrt::Windows::UI::Xaml::Controls::IControlOverrides>::OnPointerMoved(void *))", {}, false, true},
}};

inline bool supports_classic(const Plan& plan) {
    return plan.rvas[SymClassicHoverIndex] && plan.rvas[SymClassicGetItem] &&
        plan.rvas[SymClassicGetGroup] && plan.rvas[SymClassicTaskReordered] &&
        plan.rvas[SymClassicWndProc];
}
inline bool supports_modern(const Plan& plan) {
    return (plan.rvas[SymModernThumbnailCtorOld] || plan.rvas[SymModernThumbnailCtorNew]) &&
        plan.rvas[SymModernExtendedClick] && plan.rvas[SymModernPointerMoved];
}
inline bool supports_any_thumbnail(const Plan& plan) {
    return supports_classic(plan) || supports_modern(plan);
}
}
