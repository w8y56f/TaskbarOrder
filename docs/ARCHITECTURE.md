# Independent prototype architecture

## Process boundaries

`TaskbarOrder.exe` owns the UI, tray, diagnosis, networking, DbgHelp session, symbol cache and explicit enable gate. It verifies the current desktop's `Shell_TrayWnd` belongs to `%WINDIR%\explorer.exe`, and enumerates that process's loaded taskbar modules.

The controller reads each module's PE/RSDS identity, downloads a GUID+age-specific PDB from Microsoft if requested, and resolves an exact list of 20 symbols. A local plan stores RVAs alongside identities. No remote code or downloaded executable is loaded. Cached plans are versioned and checked against current files before use.

A thread-specific `WH_CALLWNDPROC` hook loads `TaskbarOrderHook-<version>.dll` into the taskbar thread. A fixed-layout shared-memory command contains identities and RVAs, never remote function pointers. The DLL checks those identities again against the live modules, checks address protections, and recognizes only explicit x64 accessor layouts before creating hooks. Symbol matching cannot prove ABI compatibility, so runtime validation is still required.

The engine uses statically linked MinHook. On the classic path it hooks the thumbnail window procedure. On the 25H2 XAML path it maps the taskbar's thumbnail model objects to task items and watches the thumbnail list's pointer movement. It captures the active TaskGroup thumbnail collection during HoverFlyoutModel updates and uses WinUI 2 ItemsRepeater indices to select models; DataContext is not used. Limited stage diagnostics record where dragging stops. A left drag obtains source/target task items from the same preview group, preflights known group/button arrays and refresh interfaces, then moves the item in both model and view. No window AppID changes are included.

## Threading and lifecycle

- Symbol work runs in one controller worker; DbgHelp is not invoked concurrently or from Explorer.
- Activation and drag mutations run on the original taskbar thread. Other threads take the original function path. Sentinel and DPA-capture state is thread-local.
- Initialization installs hooks only after all required symbol addresses and the accessor shape are validated.
- The controller's enable event resets on failed activation, cancellation, stop and close. The engine also checks the controller process handle before a drag.
- Disabling stops custom behavior. Hook code/trampolines remain resident until Explorer exits. No unsafe hot-unload is attempted.
- Explorer restart is detected by the controller; reactivation is explicit.
- Optional logon start uses the current user's `Run` registry entry and launches the controller into the notification area. It tries to enable from an existing matching symbol plan without downloading PDBs. No service, driver, UAC elevation, global process injector, executable disguise or security-product workaround is involved.

## Compatibility boundaries

Known APIs are used for bootstrap, but reordering itself relies on private Explorer/XAML implementation details. Exact symbols and bounds checks reduce mistakes; they do not make private APIs stable or prove that a particular VDI image is supported.

The implementation contains classic `TaskListThumbnailWnd` and newer XAML hover-preview paths on native x64 Windows 11. The known target is Windows 11 Enterprise 25H2 Build 26200.9445 and selects the XAML path. It does not support ARM64, custom/replacement taskbars, direct taskbar-button dragging, cross-group moves, or persisted order. Secondary taskbars are included only when they share the current taskbar thread and recognized view structures; multi-monitor/virtual-desktop behavior needs explicit validation.

Current code has been cross-compiled into Windows x64 PE files but has not been run inside Explorer. Mac logic checks are a separate, narrower result.
