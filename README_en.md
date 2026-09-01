# MultiPath Copy for Windows

[简体中文](README.md) | English

A lightweight, native Windows path-copying tool. Select multiple files or folders in File Explorer and copy all their full paths at once from the context menu.

> The current version provides Chinese menus and Chinese command-line help only.

## Features

- Copy the full paths of multiple files and folders at once
- File Explorer-style natural sorting
- Optional folders-first ordering
- Optional double quotes around paths
- Optional trailing backslashes for folder paths
- Per-user registry installation without administrator privileges
- Native C++17 implementation using only the Windows API, with no third-party dependencies

## Project Structure

```text
.
├── .github/
│   └── workflows/
│       └── release.yml             # Automated releases triggered by version tags
├── src/
│   ├── main.cpp                    # Application logic
│   └── version.rc.in               # Windows EXE version resource template
├── CHANGELOG.md                         # Release notes
├── CMakeLists.txt                       # CMake build configuration
├── build-windows-x64.bat                # Windows x64 Release build script
├── VERSION                              # Single source of truth for the version
├── README.md                            # Chinese README (default)
└── README_en.md                         # English README
```

The project currently has only one source file, so it does not split the code into additional headers and source files, avoiding unnecessary project complexity.

## Requirements

- Windows 10 or Windows 11
- x64 environment
- CMake 3.20 or later
- An MSVC toolchain with C++17 support (Visual Studio 2022 or later is recommended)

## Build

Double-click `build-windows-x64.bat`, or run it from CMD:

```bat
build-windows-x64.bat
```

The script creates a Release build in the `.build` directory and installs the final file to:

```text
output\MultiPathCopy.exe
```

You can also use CMake manually:

```powershell
cmake -S . -B .build -A x64
cmake --build .build --config Release
cmake --install .build --config Release --prefix output
```

After building, right-click `MultiPathCopy.exe` in File Explorer and select **Properties → Details** to view the file and product versions.

## Versioning and Releases

`VERSION` is the project's single source of truth for its version and must use the `MAJOR.MINOR.PATCH` format, such as `0.1.0`. CMake writes this version to the EXE properties, and the GitHub Release workflow uses it to validate tags, name ZIP files, and extract release notes.

To publish a new version:

1. Update `VERSION`.
2. Add a matching `## [x.y.z]` section at the top of `CHANGELOG.md`.
3. Commit and push the changes.
4. Create a `vx.y.z` tag for that commit, then push the tag.

For example, when `VERSION` is `0.1.0`:

```powershell
git tag -a v0.1.0 -m "发布 v0.1.0"
git push origin v0.1.0
```

The release workflow rejects tags that do not match `VERSION`. After validation, GitHub automatically builds and publishes:

- `MultiPathCopy-x.y.z-windows-x64.zip`
- `MultiPathCopy-x.y.z-SHA256SUMS.txt`

The release notes are extracted from the matching version section in `CHANGELOG.md`. The ZIP contains `MultiPathCopy.exe`, `README.md`, `CHANGELOG.md`, and `VERSION`; the checksum file contains only the ZIP's SHA-256 value.

## Installation

First copy the EXE to a permanent location, then install the context menu. The recommended location is:

```text
%LOCALAPPDATA%\MultiPathCopy\MultiPathCopy.exe
```

Run the following in PowerShell:

```powershell
$installDir = Join-Path $env:LOCALAPPDATA "MultiPathCopy"
New-Item -ItemType Directory -Force $installDir | Out-Null
Copy-Item ".\output\MultiPathCopy.exe" $installDir -Force
& (Join-Path $installDir "MultiPathCopy.exe") --install
& (Join-Path $installDir "MultiPathCopy.exe") --init-config
```

After installation, select one or more files or folders in File Explorer, then choose **复制完整路径** (Copy Full Paths) from the context menu.

Do not move, rename, or delete the EXE after installing the context menu, or the menu command will stop working.

## Command-Line Options

| Command | Description |
| --- | --- |
| `MultiPathCopy.exe --help` | Display Chinese usage information |
| `MultiPathCopy.exe --install` | Install context-menu entries for files and folders |
| `MultiPathCopy.exe --uninstall` | Remove the context-menu entries created by this tool |
| `MultiPathCopy.exe --init-config` | Create or overwrite the default configuration |
| `MultiPathCopy.exe --config` | Display the configuration file location |

## Configuration

The configuration file is located at:

```text
%LOCALAPPDATA%\MultiPathCopy\settings.ini
```

Default configuration:

```ini
[copy]
quote_paths=true
folder_trailing_slash=true
folders_first=true
sort_mode=explorer
```

| Option | Description |
| --- | --- |
| `quote_paths` | Wrap each path in double quotes |
| `folder_trailing_slash` | Add a trailing backslash to folder paths |
| `folders_first` | Place all folders before files |
| `sort_mode` | `explorer` uses natural sorting; `none` preserves the order in which paths arrive |

## Uninstallation

Run the following in PowerShell:

```powershell
& "$env:LOCALAPPDATA\MultiPathCopy\MultiPathCopy.exe" --uninstall
```

After confirming that the context menu has been removed, you can manually delete the `%LOCALAPPDATA%\MultiPathCopy` directory.

## Implementation Notes and Limitations

Standard Windows registry context-menu commands start a separate process for each selected item. MultiPath Copy uses shared memory, a mutex, and an event scoped to the current user and session to collect these paths over a short interval. A single process then sorts them and writes them to the clipboard.

This approach is suitable for everyday multi-selection workloads. Shared memory is currently limited to 64 KiB, so extremely large selections are not guaranteed to fit. A COM Shell Extension would be more suitable for reliably processing thousands of items.
