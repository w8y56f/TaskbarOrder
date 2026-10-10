# Windows verification checklist

Status: not run. Do not mark a row passed based on Mac tests or successful compilation.

## Build and environment

- [ ] MSVC x64 Release build succeeds, including the C++/WinRT engine.
- [ ] Windows CTest passes.
- [ ] EXE and DLL run on Windows 11 without installing .NET or VC redistributables.
- [x] Record `winver` release/build: Windows 11 Enterprise 25H2, OS Build 26200.9445.
- [ ] Confirm x64 architecture and record both module PDB GUID/age values.
- [ ] Detect reports only this session's standard Explorer taskbar.
- [ ] Missing/unsupported modules cause a clear message and no activation.
- [ ] First symbol preparation downloads matching Microsoft PDBs; cached preparation works offline.
- [ ] Mismatched or truncated symbol plan is rejected.
- [ ] Unknown accessor layout is rejected before any hooks are installed.

## Core interaction

Use disposable application windows for the first live check. Keep taskbar buttons combined, hover the application icon, and operate on the preview cards.

- [ ] Three preview cards A/B/C: drag A across B -> B/A/C, then across C -> B/C/A.
- [ ] Drag C left across B and A -> C/A/B; the order updates while dragging.
- [ ] Adjacent moves, click without dragging, and release outside the preview preserve the intended order.
- [ ] Drag to a different application causes no regrouping or AppID change.
- [ ] Ordinary left click, right click, focus, minimize and close still work.
- [ ] Moving without the left button pressed does not reorder.
- [ ] Close a source/target window during dragging; no Explorer crash or stale-pointer behavior.
- [ ] New windows append normally after prior reorders.

## Lifecycle and VDI

- [ ] Stop restores normal interaction; enable again works.
- [ ] Close/force-end controller; custom dragging stops on subsequent input.
- [ ] Timed-out/failed activation does not leave enabled custom behavior.
- [ ] Explorer restart removes the prior DLL and controller reports inactive.
- [ ] Re-enable after restart; no stale addresses used.
- [ ] Multiple monitors and per-monitor window visibility.
- [ ] Virtual desktops and moving a window between desktops.
- [ ] VDI disconnect/reconnect, resolution/DPI changes, and session lock/unlock.

Report `environment.txt`, `controller.log`, `engine.log`, the operation sequence and observed result. The logs intentionally omit window titles. Do not claim general Windows 11 compatibility after passing only one build.
