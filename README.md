# MultiPath Copy for Windows

一个轻量、原生的 Windows 路径复制工具。在文件资源管理器中选中多个文件或文件夹后，可通过右键菜单一次复制所有完整路径。

> 当前版本只提供中文菜单和中文命令行说明。

## 功能特性

- 同时复制多个文件和文件夹的完整路径
- 使用资源管理器风格的自然排序
- 可选文件夹优先排列
- 可选为路径添加双引号
- 可选为文件夹路径添加末尾反斜杠
- 通过当前用户注册表安装，无需管理员权限
- 原生 C++17 实现，仅使用 Windows API，无第三方库依赖

## 项目结构

```text
.
├── .github/
│   └── workflows/
│       └── release.yml             # 版本 Tag 触发自动发布
├── src/
│   ├── main.cpp                       # 应用逻辑
│   └── version.rc.in                  # Windows EXE 版本资源模板
├── CHANGELOG.md                           # 版本更新说明
├── CMakeLists.txt                # CMake 构建配置
├── build-windows-x64.bat         # Windows x64 Release 构建脚本
├── VERSION                                # 唯一版本号来源
└── README.md
```

项目目前只有一个源文件，因此暂不拆分额外的头文件和源文件，避免不必要的工程复杂度。

## 系统要求

- Windows 10 或 Windows 11
- x64 环境
- CMake 3.20 或更高版本
- 支持 C++17 的 MSVC 工具链（推荐 Visual Studio 2022 或更高版本）

## 构建

直接双击 `build-windows-x64.bat`，或在 CMD 中执行：

```bat
build-windows-x64.bat
```

脚本会在 `.build` 目录中生成 Release 构建，并将最终文件安装到：

```text
output\MultiPathCopy.exe
```

也可以手动使用 CMake：

```powershell
cmake -S . -B .build -A x64
cmake --build .build --config Release
cmake --install .build --config Release --prefix output
```

构建完成后，可在资源管理器中右击 `MultiPathCopy.exe`，选择“属性 → 详细信息”查看文件版本和产品版本。

## 版本与发布

`VERSION` 是项目的唯一版本号来源，格式必须为 `MAJOR.MINOR.PATCH`，例如 `0.1.0`。CMake 会将该版本写入 EXE 属性，GitHub Release 工作流也会使用它校验 Tag、命名 ZIP 和提取更新说明。

要发布新版本：

1. 修改 `VERSION`。
2. 在 `CHANGELOG.md` 顶部增加对应版本的 `## [x.y.z]` 章节。
3. 提交并 push 代码。
4. 为该提交创建 `vx.y.z` Tag，然后 push Tag。

例如 `VERSION` 为 `0.1.0` 时：

```powershell
git tag -a v0.1.0 -m "发布 v0.1.0"
git push origin v0.1.0
```

Release 工作流会拒绝与 `VERSION` 不一致的 Tag。校验通过后，GitHub 会自动构建并发布：

- `MultiPathCopy-x.y.z-windows-x64.zip`
- `MultiPathCopy-x.y.z-SHA256SUMS.txt`

Release 页面的更新说明会从 `CHANGELOG.md` 中对应版本的章节自动提取。ZIP 内包含 `MultiPathCopy.exe`、`README.md`、`CHANGELOG.md` 和 `VERSION`；校验文件只记录 ZIP 的 SHA-256 值。

## 安装

先将 EXE 复制到一个固定位置，再安装右键菜单。推荐使用：

```text
%LOCALAPPDATA%\MultiPathCopy\MultiPathCopy.exe
```

可在 PowerShell 中执行：

```powershell
$installDir = Join-Path $env:LOCALAPPDATA "MultiPathCopy"
New-Item -ItemType Directory -Force $installDir | Out-Null
Copy-Item ".\output\MultiPathCopy.exe" $installDir -Force
& (Join-Path $installDir "MultiPathCopy.exe") --install
& (Join-Path $installDir "MultiPathCopy.exe") --init-config
```

安装完成后，在资源管理器中选中一个或多个文件、文件夹，然后在右键菜单中选择“复制完整路径”。

安装右键菜单后，请不要移动、重命名或删除 EXE，否则菜单命令将失效。

## 命令行选项

| 命令 | 作用 |
| --- | --- |
| `MultiPathCopy.exe --help` | 显示中文使用说明 |
| `MultiPathCopy.exe --install` | 为文件和文件夹安装右键菜单 |
| `MultiPathCopy.exe --uninstall` | 卸载本工具创建的右键菜单 |
| `MultiPathCopy.exe --init-config` | 创建或覆盖默认配置 |
| `MultiPathCopy.exe --config` | 显示配置文件位置 |

## 配置

配置文件位于：

```text
%LOCALAPPDATA%\MultiPathCopy\settings.ini
```

默认配置：

```ini
[copy]
quote_paths=true
folder_trailing_slash=true
folders_first=true
sort_mode=explorer
```

| 选项 | 说明 |
| --- | --- |
| `quote_paths` | 为每条路径添加双引号 |
| `folder_trailing_slash` | 为文件夹路径添加末尾反斜杠 |
| `folders_first` | 将文件夹整体排在文件之前 |
| `sort_mode` | `explorer` 使用自然排序；`none` 保留路径到达顺序 |

## 卸载

在 PowerShell 中执行：

```powershell
& "$env:LOCALAPPDATA\MultiPathCopy\MultiPathCopy.exe" --uninstall
```

确认右键菜单已移除后，可手动删除 `%LOCALAPPDATA%\MultiPathCopy` 目录。

## 实现说明与限制

Windows 的普通注册表右键命令会为多个选中项分别启动进程。MultiPath Copy 使用当前用户和会话专属的共享内存、互斥锁与事件，在短时间内汇聚这些路径，再由单个进程排序并写入剪贴板。

这种方案适合日常规模的多选操作。共享内存当前上限为 64 KiB，不保证超大选择集的路径能全部写入；如果需要稳定处理成千上万个项目，更适合实现 COM Shell Extension。
