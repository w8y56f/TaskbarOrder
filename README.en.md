# TaskbarOrder

[简体中文](README.md) | **English**

An experimental Windows 11 taskbar thumbnail reordering tool, independent of Windhawk. Written in C++ / Win32 and distributed as a portable EXE + DLL. No .NET, installed service, or administrator elevation request.

**Experimental prototype, not a verified finished product.** Version 0.9.0 includes new thumbnail mapping, an About dialog, notification-area behavior, an application icon, and bilingual documentation. The Windows x64 build and automated checks run during release; actual dragging still needs validation on the target VDI. Version 0.1.3 activated successfully there, but dragging did not reorder thumbnails.

Download: [GitHub Releases](https://github.com/w8y56f/TaskbarOrder/releases). This README describes the current source and version 0.9.0 behavior.

## Features and scope

- Targets the native taskbar on x64 Windows 11.
- Hover over an application icon, then **drag a preview card with the left mouse button** to reorder windows of that application.
- Taskbar buttons can remain grouped; disabling grouping is not required.
- Includes both classic `TaskListThumbnailWnd` and newer XAML preview paths. The known target is Windows 11 Enterprise 25H2, build 26200.9445, using the XAML path.
- Moving windows between application groups, changing AppUserModelID, dragging taskbar buttons directly, and preserving order across restarts are outside the current scope.
- Activation is rejected if symbols or internal layouts do not match. Passing these checks does not prove runtime compatibility.

The confirmed use case is reordering the window preview cards shown when hovering over an application icon. The target VDI runs Windows 11 Enterprise 25H2, OS build 26200.9445, on x64.

The root [`VERSION`](VERSION) file is the single source of the software version. CMake uses it for the UI version, Windows EXE/DLL file versions, manifest, and versioned engine DLL name. Packaging scripts use it for release filenames.

## Repository layout

```text
src/controller/   UI, notification area, environment diagnosis, Microsoft symbol cache
src/engine/       Reordering engine loaded into the current session's Explorer taskbar thread
src/common/       IPC protocol, module identity checks, ordering logic
third_party/      Pinned MinHook source, linked statically
scripts/          Windows build/package scripts and optional offline symbol download
tests/            Logic checks that can run on a Mac
docs/             Architecture, limitations, and Windows validation steps
```

## Build on Windows

On a separate Windows development machine, install Visual Studio 2022 or Build Tools with Desktop development with C++, the MSVC x64 toolchain, a Windows SDK (10.0.22621 or newer recommended, including C++/WinRT headers), and CMake 3.24+. These tools do not need to be installed on the VDI.

Run from the project directory in PowerShell:

```powershell
./scripts/build-windows.ps1
```

The script downloads and verifies Microsoft.UI.Xaml 2.8.7 and its WebView2 metadata dependency to generate WinUI 2 projection headers. These build dependencies do not need to be installed on the VDI.

The output is `dist/TaskbarOrder-<VERSION>-win11-x64.zip`, containing the EXE, DLL, version file, licenses, documentation, and corresponding source. A GitHub Actions Windows build is also configured.

Prefer this script so the Windows SDK and WinUI projections use the same C++/WinRT generator. Direct CMake builds must provide `CPPWINRT_INCLUDE_DIR` containing both sets of projections.

## What can be done on a Mac?

Edit all source files and run platform-independent checks:

```sh
cmake -S . -B build-mac -DBUILD_TESTING=ON
cmake --build build-mac
ctest --test-dir build-mac --output-on-failure
```

This does not produce a Windows EXE or validate Windows APIs, XAML, DLL loading, or Explorer integration. It does not install .NET or automatically download Windows build tools.

## Running the application

The UI currently uses Chinese labels; English descriptions are provided below.

### First use

**Order: extract everything → run the EXE → wait for environment detection → Prepare symbols → wait for success → Enable reordering → drag preview cards.**

1. **Extract the entire ZIP into a normal folder.** Keep `TaskbarOrder.exe` and `TaskbarOrderHook-<version>.dll` together. Do not run the EXE from inside the archive or copy only the EXE. No installation is required.
2. **Double-click `TaskbarOrder.exe` and wait for environment detection.** Detection runs automatically, so there is no need to click the first button again. Confirm that the output identifies Windows 11 and native x64. Use **检测环境 (Detect environment)** only to repeat detection. This step runs locally without network access.
3. **Click the second button, 准备符号 (Prepare symbols), and wait patiently.** On first use, the application downloads matching public PDB symbol files from Microsoft, resolves the functions, and caches the results. This is usually the slowest first-use step. It may take several minutes, or longer on a slow connection, depending on network conditions and file sizes.

   > **“正在从 Microsoft 下载…” (Downloading from Microsoft) or “正在匹配…” (Matching) does not necessarily mean the application is stuck.** There is currently no download percentage, so the same message may remain visible for some time. Disabled buttons are also normal while processing. Keep the application running and the network connected; do not repeatedly click or launch another instance. Continue only after “符号已匹配并缓存。可以点击‘启用调序’” (Symbols matched and cached; you can enable reordering) appears. If “未完成” (Not completed) or a download error appears, use Open logs. If there is no progress for a long time, inspect the logs and network connection as well. For an offline environment, see Offline symbol preparation below.

4. **Click the third button, 启用调序 (Enable reordering).** Wait for “已启用 … 预览支持” (Preview support enabled). If an error appears, check the logs. Successful symbol preparation does not itself enable reordering.
5. **Drag previews of windows belonging to the same application.** For example, open two separate IDEA windows and hover over the IDEA taskbar icon. Hold the left mouse button on the preview content of one card, drag it to the other card, and release. Drag the popup preview card, not the application icon on the taskbar, and avoid the card's close button. If the preview was already open before activation, move the pointer away to close it, then hover again.

“Enabled” means initialization passed; actual dragging still needs validation on the target system. If the cards do not reorder, click **打开日志 (Open logs)** and inspect the new `engine.log` entries from that attempt.

### Subsequent use

- After quitting and relaunching, wait for automatic detection. If the cache matches, **click Enable reordering directly**; preparing symbols on every launch is unnecessary.
- If you only restore a window that is still running in the notification area and reordering is already active, no further action is needed.
- Windows updates, a cleared cache, or newly required symbols may require another Prepare symbols step. Existing PDB downloads can be reused. Upgrading from version 0.1.3 or earlier to 0.9.0 requires preparing symbols once more.

### Disable, hide, and exit

Click **停用调序 (Disable reordering)** to stop reordering. Minimizing or clicking × hides the window in the notification area and leaves it running. Double-click that icon to restore the window. Use **设置 → 退出 (Settings → Exit)** or the notification-area context menu to quit completely.

**设置 → 关于 (Settings → About)** displays the application name, version, build commit ID, and “Powered by Stone Wang”.

Logs and symbols are stored in `%LOCALAPPDATA%\TaskbarOrder`; **打开日志 (Open logs)** opens that directory. Environment reports do not include window titles, document contents, or a full process list.

### Offline symbol preparation

On first use, activation is not possible without either a matching cache or network access. Symbol files locate internal functions; no window contents are uploaded. Symbol resolution runs in the EXE, and Explorer does not perform network requests for it.

If the VDI cannot access the symbol server, transfer its generated `environment.json` to a Mac with network access and run:

```sh
python3 scripts/download-symbols.py environment.json --output symbols
```

Copy the resulting `symbols` directory to `%LOCALAPPDATA%\TaskbarOrder\symbols` on the VDI, then click Prepare symbols to use the local cache. This handles public PDB files only; resolution still takes place on Windows. Use a transfer method allowed in your environment.

## Lifecycle and limitations

After disabling, installed hooks forward to the original functions. The DLL remains idle in Explorer until Explorer exits, avoiding unloading code that may still be executing. The application does not automatically restart Explorer. If the controller exits unexpectedly, the reordering entry point also becomes inactive when it detects the owner process has exited.

On later launches, wait for automatic detection, then click Enable reordering directly if the cache matches. Upgrading from version 0.1.3 or earlier to 0.9.0 requires preparing symbols once more; existing PDB downloads can be reused.

After Explorer restarts, re-enable manually. Windows updates that change module identities require preparing symbols again. When changing engine DLL versions, restart Explorer or sign out and back in; an already resident engine cannot be replaced in place.

Actual dragging, ordinary clicks/context menus, multiple monitors, virtual desktops, VDI reconnection, and compatibility across Windows updates still need validation. See the [validation checklist](docs/VALIDATION.md).

## Bilingual documentation and release notes

Chinese documentation is maintained in [README.md](README.md), and English documentation in `README.en.md`. Keep both in sync. Both files are included in the portable package and its corresponding source.

Each version has one GitHub Release and one `v<version>` tag. Within the release notes, **中文 → English** sections cover changes, upgrade steps, and validation status. Both languages share the same portable ZIP package; no duplicate builds are needed. Bilingual documentation does not imply an English application UI. See the [release notes template](docs/RELEASE_NOTES_TEMPLATE.md).

## License

This project is licensed under GPL-3.0-only. Its reordering algorithm and internal symbol declarations derive from m417z's open-source mod. MinHook uses its BSD-style license. Sources and licenses are retained in the [third-party notices](THIRD_PARTY_NOTICES.md). Windhawk is not required to run this project.
