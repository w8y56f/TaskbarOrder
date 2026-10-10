# TaskbarOrder

一个不依赖 Windhawk 的 Windows 11 任务栏调序实验原型。C++ / Win32，便携 EXE + DLL，无 .NET、无安装服务、无管理员权限申请。

**当前版本：0.1.0 实验原型。已交叉编译为 Windows x64 EXE + DLL，但尚未在目标 Windows 11 VDI 中运行。不能视为已验证的成品。**

## 第一版范围

- 目标为原生 x64 Windows 11 任务栏。
- 鼠标悬停在一个应用图标上；预览弹出后，用**左键拖动预览卡片**调整同一应用窗口的顺序。
- Windows 任务栏按钮可以保持合并；无需切换到“不合并”。
- 包含经典 `TaskListThumbnailWnd` 和较新的 XAML 预览两条路径。已知目标环境为 Windows 11 Enterprise 25H2、Build 26200.9445，将选择 XAML 路径。
- 跨应用分组、修改 AppUserModelID、直接拖动任务栏按钮和重启后记忆排序均不在此版范围。
- 符号或内部布局不匹配时拒绝启用。匹配成功只代表前置检查通过，不代表运行兼容性已验证。

需求已确认：目标是悬停应用图标后出现的多个窗口预览卡片之间调序。VDI 版本为 Windows 11 Enterprise 25H2、OS Build 26200.9445，处理器架构为 x64。

根目录的 [`VERSION`](VERSION) 是唯一的软件版本来源。CMake 使用它生成程序界面版本、Windows EXE/DLL 文件版本、manifest 和带版本号的核心 DLL；打包脚本也使用它生成发布文件名。

## 文件结构

```text
src/controller/   控制界面、托盘、环境诊断、Microsoft 符号缓存
src/engine/       仅加载到当前会话 Explorer 任务栏线程的调序核心
src/common/       进程间协议、模块身份校验、排序规则
third_party/      固定版本的 MinHook，静态链接
scripts/          Windows 构建/打包和可选离线符号下载
tests/            可在 Mac 执行的逻辑检查
docs/            架构、已知限制和 Windows 验证步骤
```

## Windows 构建

在独立 Windows 开发环境安装 Visual Studio 2022 或 Build Tools，选择“使用 C++ 的桌面开发”、MSVC x64 工具链、Windows SDK（建议 10.0.22621 或更新，包含 C++/WinRT 头文件）和 CMake 3.24+。这些工具不需要装到 VDI。

从 PowerShell 在项目目录执行：

```powershell
./scripts/build-windows.ps1
```

脚本在构建时下载并校验 Microsoft.UI.Xaml 2.8.7 元数据，生成 WinUI 2 投影头文件；VDI 不需要安装它。

脚本生成 `dist/TaskbarOrder-<VERSION>-win11-x64.zip`，包含 EXE、DLL、版本文件、许可证、说明和对应源代码。项目已配置 GitHub Actions Windows 构建。

请优先使用上述脚本，以确保 Windows SDK 和 WinUI 投影由同一版 C++/WinRT 生成。直接使用 CMake 时必须传入包含这两组投影的 `CPPWINRT_INCLUDE_DIR`。

## Mac 上目前能做什么

编辑所有源码，并执行平台无关检查：

```sh
cmake -S . -B build-mac -DBUILD_TESTING=ON
cmake --build build-mac
ctest --test-dir build-mac --output-on-failure
```

这不会生成 Windows EXE，也不会验证 Windows API、XAML、DLL 加载或 Explorer 交互。没有安装 .NET，也没有自动下载 Windows 编译工具。

## 构建后的操作

1. 解压，保持 `TaskbarOrder.exe` 和 `TaskbarOrderHook-0.1.0.dll` 在同一目录。
2. 运行 EXE；默认只检测环境，不自动启用 Hook。
3. 点击“准备符号”。首次仅从 Microsoft 下载两个模块匹配的公开 PDB；不上传窗口内容。符号解析在 EXE 中完成，不在 Explorer 内联网。
4. 点击“启用调序”。悬停应用图标，在弹出的预览卡片上按住左键拖动。
5. “停用调序”或关闭控制程序可停止调序；最小化收进托盘。

日志和符号位于 `%LOCALAPPDATA%\TaskbarOrder`；“打开日志”可以打开该目录。环境报告不包含窗口标题、文档内容或完整进程清单。

VDI 不能访问符号服务器时，可把生成的 `environment.json` 带到 Mac，运行：

```sh
python3 scripts/download-symbols.py environment.json --output symbols
```

再把 `symbols` 目录复制到 VDI 的 `%LOCALAPPDATA%\TaskbarOrder\symbols`，点击“准备符号”即可使用本地缓存。这只处理公开 PDB，仍需在 Windows 上解析；具体传输方式遵循你的环境要求。

## 生命周期与限制

停用后，已安装的 Hook 走回原函数，DLL 保持空闲驻留，直到 Explorer 退出，以避免卸载仍在执行的函数。程序不会自动重启 Explorer。控制程序异常退出后，调序入口也会因所有者进程已退出而失效。

日常启动后等自动检测结束，缓存匹配时直接点击“启用调序”即可。0.1.4 新增符号项，升级后需点一次“准备符号”（已有 PDB 可复用）。

Explorer 重启后需要手动重新启用；Windows 更新导致模块身份变化时，需要重新准备符号。更换核心 DLL 版本后需要重启 Explorer 或注销再登录，不能热替换已驻留的核心。

实际拖动、普通点击/右键菜单、多个显示器、虚拟桌面、VDI 重连和不同 Windows 补丁的兼容性尚待验证。详见 [验证清单](docs/VALIDATION.md)。

## 许可证

本项目依据 GPL-3.0-only 发布；调序算法和内部符号声明派生自 m417z 的开源模组。MinHook 使用其 BSD 风格许可证。保留了来源与许可证，详见 [第三方说明](THIRD_PARTY_NOTICES.md)。运行本项目不需要 Windhawk。
