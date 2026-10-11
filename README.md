# TaskbarOrder

**简体中文** | [English](README.en.md)

一个不依赖 Windhawk 的 Windows 11 任务栏调序实验原型。C++ / Win32，便携 EXE + DLL，无 .NET、无安装服务、无管理员权限申请。

**实验原型，尚不能视为已验证的成品。** 0.9.0 包含新的缩略图映射、关于页、通知区域行为、应用图标及双语文档。Windows x64 构建和自动检查会在发布时执行；实际拖动效果仍待目标 VDI 验证。0.1.3 曾在目标 VDI 成功启用，但拖动未生效。

下载：[GitHub Releases](https://github.com/w8y56f/TaskbarOrder/releases)。本文描述当前源码和 0.9.0 功能。

## 功能与范围

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

脚本在构建时下载并校验 Microsoft.UI.Xaml 2.8.7 及其配套 WebView2 元数据，生成 WinUI 2 投影头文件；VDI 不需要安装这些构建依赖。

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

## 使用步骤

### 第一次使用

**顺序：完整解压 → 运行 EXE → 等待检测环境完成 → 准备符号 → 等待准备成功 → 启用调序 → 拖动预览卡片。**

1. **完整解压。** 将下载的 ZIP 解压到普通文件夹，保持 `TaskbarOrder.exe` 和 `TaskbarOrderHook-<版本号>.dll` 在同一目录。不要直接在压缩包里运行 EXE，也不要只复制 EXE。无需安装。
2. **双击 `TaskbarOrder.exe`，等待“检测环境”完成。** 程序启动时会自动检测，无需重复点击第一个按钮。确认输出显示 Windows 11、原生 x64；需要重新检测时才点击“检测环境”。这一步在本机完成，不需要联网。
3. **点击第二个按钮“准备符号”，然后耐心等待。** 首次需要从 Microsoft 下载与当前任务栏组件匹配的公开 PDB 符号文件，再进行匹配和缓存。这通常是首次启动中最耗时的一步，可能需要几分钟，网络较慢时更久，具体取决于网络和文件大小。

   > **显示“正在从 Microsoft 下载…”或“正在匹配…”，不一定是卡住了。** 当前界面没有下载百分比，同一行提示可能停留较久；处理时按钮变灰也是正常现象。请保持程序运行和网络连接，不要反复点击或重复启动。等到出现“符号已匹配并缓存。可以点击‘启用调序’”后，再进行下一步。如果提示“未完成”或下载错误，请查看“打开日志”；长时间没有进展时也可查看日志和网络情况。离线环境请使用下方的“离线准备符号”方法。

4. **点击第三个按钮“启用调序”。** 等待出现“已启用 … 预览支持”。如果出现错误，先查看日志，不要把“准备符号成功”当成已经启用。
5. **拖动同一应用的窗口预览。** 以 IDEA 为例，先打开两个独立窗口，将鼠标悬停在任务栏的 IDEA 图标上。预览弹出后，在其中一张卡片的预览内容区域按住鼠标左键，拖到另一张卡片的位置，再松开。拖动的是弹出的预览卡片，不是任务栏上的应用图标；避开卡片的关闭按钮。若启用前预览已打开，先移开鼠标关闭预览，再悬停打开。

“已启用”表示初始化通过，实际拖动仍需在目标系统验证。如果卡片没有调序，请点击“打开日志”，查看本次操作新增的 `engine.log` 记录。

### 以后再次使用

- 正常退出后再次运行：等待自动检测结束，缓存匹配时**直接点击“启用调序”**，无需每次点击“准备符号”。
- 只是从通知区域恢复仍在运行的窗口：如果调序已经启用，无需重复操作。
- Windows 更新、缓存被清除或新版新增符号项后，可能需要重新“准备符号”。已有的 PDB 下载缓存可以复用；从 0.1.3 或更早版本升级到 0.9.0 时，需要重新准备一次。

### 停用、隐藏与退出

点击“停用调序”可停止调序。最小化或点击 × 会收进通知区域并继续运行，双击通知区域图标可恢复窗口；通过“设置 → 退出”或托盘右键菜单彻底退出。

“设置 → 关于”显示软件名称、版本、构建提交 ID 和 Powered by Stone Wang。

日志和符号位于 `%LOCALAPPDATA%\TaskbarOrder`；“打开日志”可以打开该目录。环境报告不包含窗口标题、文档内容或完整进程清单。

### 离线准备符号

首次没有匹配缓存且无法联网时，不能直接启用。符号文件只用于定位内部函数，不上传窗口内容；符号解析在 EXE 中完成，不在 Explorer 内联网。

VDI 不能访问符号服务器时，可把生成的 `environment.json` 带到 Mac，运行：

```sh
python3 scripts/download-symbols.py environment.json --output symbols
```

再把 `symbols` 目录复制到 VDI 的 `%LOCALAPPDATA%\TaskbarOrder\symbols`，点击“准备符号”即可使用本地缓存。这只处理公开 PDB，仍需在 Windows 上解析；具体传输方式遵循你的环境要求。

## 生命周期与限制

停用后，已安装的 Hook 走回原函数，DLL 保持空闲驻留，直到 Explorer 退出，以避免卸载仍在执行的函数。程序不会自动重启 Explorer。控制程序异常退出后，调序入口也会因所有者进程已退出而失效。

日常启动后等自动检测结束，缓存匹配时直接点击“启用调序”即可。从 0.1.3 或更早版本升级到 0.9.0 后需点一次“准备符号”（已有 PDB 可复用）。

Explorer 重启后需要手动重新启用；Windows 更新导致模块身份变化时，需要重新准备符号。更换核心 DLL 版本后需要重启 Explorer 或注销再登录，不能热替换已驻留的核心。

实际拖动、普通点击/右键菜单、多个显示器、虚拟桌面、VDI 重连和不同 Windows 补丁的兼容性尚待验证。详见 [验证清单](docs/VALIDATION.md)。

## 双语文档与发布说明

中文说明位于 `README.md`，英文说明位于 [README.en.md](README.en.md)，更新时同步维护。两份说明都会包含在便携包和对应源代码中。

每个版本只创建一条 GitHub Release、一个 `v<版本号>` 标签，在同一条发布说明中按 **中文 → English** 排列更新内容、升级步骤和验证状态。两种语言共用同一个 ZIP 便携包，不重复构建。说明采用双语并不代表软件界面已支持英文。发布格式见 [发布说明模板](docs/RELEASE_NOTES_TEMPLATE.md)。

## 许可证

本项目依据 GPL-3.0-only 发布；调序算法和内部符号声明派生自 m417z 的开源模组。MinHook 使用其 BSD 风格许可证。保留了来源与许可证，详见 [第三方说明](THIRD_PARTY_NOTICES.md)。运行本项目不需要 Windhawk。
