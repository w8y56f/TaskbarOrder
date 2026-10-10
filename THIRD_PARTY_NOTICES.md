# Third-party code and attribution

## Taskbar Thumbnail Reorder

- Author: Michael / m417z (Ramen Software).
- Original version: 1.1.5.
- License: GNU General Public License version 3.0 (GPL-3.0-only); full text in `LICENSE`.
- Source: https://github.com/ramensoftware/windhawk-mods/blob/2e421932847184af2d4b2ed99a7fb28782a92808/mods/taskbar-thumbnail-reorder.wh.cpp
- Downloaded source SHA-256: `e1847e6a787402c8abd0f84c5651e839e3d93de832e6764a9549c6cc1cd70207`.
- Derived portions: internal taskbar symbol declarations, DPA capture, group/view reordering, and classic thumbnail-window dragging in `src/engine/reorder.cpp` and `src/common/symbol_catalog.hpp`.

Changes in this project: independent controller/DLL lifecycle; removed Windhawk APIs; local exact-PDB identity checks and symbol preparation; retained classic and newer XAML hover-preview paths; uses the XAML `ItemsRepeater` live indices and captured thumbnail collection for window mapping; added pointer/layout checks and an explicit accessor decoder; controller lifetime gating; logs and diagnostics. The original author has not reviewed or endorsed this derivative.

No Windhawk executable, service or SDK is required at runtime or build time.

## MinHook

- Author: Tsuda Kageyu and contributors.
- Version: v1.3.4.
- Source: https://github.com/TsudaKageyu/minhook/tree/v1.3.4
- Archive SHA-256: `1aebeae4ca898330c507860acc2fca2eb335fe446a3a2b8444c3bf8b2660a14e`.
- Vendored unmodified in `third_party/minhook`; license in `third_party/minhook/LICENSE.txt`, including the HDE notices.
- Linked statically; users do not need to install MinHook.

The build script packages corresponding project source and notices with this experimental binary distribution. Microsoft Windows libraries and public symbols are not included in the source distribution.

## Microsoft.UI.Xaml (build metadata only)

- Version: 2.8.7; official package: https://www.nuget.org/packages/Microsoft.UI.Xaml/2.8.7
- Package SHA-256: `79207b10fe243eb1a8dcdc29beceba2f472f145ed31f147f7ae3f43b0659c9f7`.
- The build script generates C++/WinRT projections from its metadata. The package is subject to its bundled Microsoft Windows UI Library license, copied to `licenses/Microsoft.UI.Xaml.txt` in the binary package.
- No WinUI runtime binaries are redistributed or installed. The engine queries the ItemsRepeater already provided by the Windows taskbar.
