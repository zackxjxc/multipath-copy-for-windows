#define UNICODE
#define _UNICODE

#include <windows.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kMenuKeyName[] = L"MultiPathCopy";
constexpr wchar_t kMenuText[] = L"复制完整路径";
constexpr wchar_t kUsageText[] =
    L"MultiPath Copy for Windows\n\n"
    L"用法：\n\n"
    L"MultiPathCopy.exe --install\n"
    L"    为文件和文件夹安装右键菜单。\n\n"
    L"MultiPathCopy.exe --uninstall\n"
    L"    卸载本工具创建的右键菜单。\n\n"
    L"MultiPathCopy.exe --init-config\n"
    L"    创建或覆盖默认配置文件。\n\n"
    L"MultiPathCopy.exe --config\n"
    L"    显示配置文件的位置。\n\n"
    L"安装后，在资源管理器中选中一个或多个文件、文件夹，\n"
    L"然后在右键菜单中选择“复制完整路径”。\n\n"
    L"注意：安装右键菜单后请勿移动、重命名或删除 EXE。\n"
    L"建议位置：%LOCALAPPDATA%\\MultiPathCopy\\MultiPathCopy.exe\n"
    L"配置文件：%LOCALAPPDATA%\\MultiPathCopy\\settings.ini\n";
constexpr wchar_t kDefaultConfigText[] =
    L"; MultiPath Copy 配置\n"
    L"[copy]\n"
    L"; 是否给每条复制路径添加双引号。\n"
    L"quote_paths=true\n"
    L"\n"
    L"; 是否给复制的文件夹路径添加末尾反斜杠。\n"
    L"folder_trailing_slash=true\n"
    L"\n"
    L"; 是否让文件夹整体排在文件前面。\n"
    L"folders_first=true\n"
    L"\n"
    L"; explorer 使用资源管理器自然排序；none 保留到达顺序。\n"
    L"sort_mode=explorer\n";
constexpr wchar_t kMagic[] = L"MPC1";
constexpr DWORD kSharedVersion = 1;
constexpr DWORD kMappingSize = 64 * 1024;
constexpr DWORD kDebounceMilliseconds = 150;
constexpr DWORD kReadyWaitMilliseconds = 2'000;

// 共享内存头部固定保存当前批次的 UTF-16 路径数据状态。
struct SharedHeader {
    wchar_t magic[5];
    DWORD version;
    DWORD usedBytes;
    DWORD pathCount;
    DWORD capacityBytes;
};

// 当前用户和会话专属的内核对象名称。
struct ObjectNames {
    std::wstring mutex;
    std::wstring mapping;
    std::wstring readyEvent;
    std::wstring changedEvent;
};

// 用于排序的路径及其文件夹属性。
struct SortablePath {
    std::wstring value;
    bool isDirectory;
};

// 当前复制批次的用户可配置行为。
struct CopyOptions {
    bool quotePaths = true;
    bool folderTrailingSlash = true;
    bool foldersFirst = true;
    bool explorerSort = true;
};

// 将文本写入父进程的控制台。
void WriteToParentConsole(const std::wstring& text) {
    const bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    if (output != nullptr && output != INVALID_HANDLE_VALUE) {
        WriteConsoleW(output, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
    }
    if (attached) {
        FreeConsole();
    }
}

// 显示直接启动程序时的命令行用法说明。
void ShowUsage() {
    WriteToParentConsole(kUsageText);
}

// 输出命令执行结果，便于从终端直接确认操作是否完成。
void ShowCommandResult(bool success, const std::wstring& message) {
    WriteToParentConsole((success ? L"成功：" : L"错误：") + message + L"\n");
}

// 获取当前用户配置文件所在的固定目录。
std::wstring GetConfigFilePath() {
    wchar_t localAppData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::wstring(localAppData, length) + L"\\MultiPathCopy\\settings.ini";
}

// 从 INI 读取布尔值，无法识别时保留默认值。
bool ReadBooleanSetting(const std::wstring& configPath, const wchar_t* key, bool defaultValue) {
    wchar_t value[16] = {};
    GetPrivateProfileStringW(L"copy", key, L"", value, _countof(value), configPath.c_str());
    if (_wcsicmp(value, L"true") == 0 || _wcsicmp(value, L"yes") == 0 || wcscmp(value, L"1") == 0) {
        return true;
    }
    if (_wcsicmp(value, L"false") == 0 || _wcsicmp(value, L"no") == 0 || wcscmp(value, L"0") == 0) {
        return false;
    }
    return defaultValue;
}

// 仅由 leader 在写入剪贴板前加载当前用户的复制设置。
CopyOptions LoadCopyOptions() {
    const std::wstring configPath = GetConfigFilePath();
    CopyOptions options;
    if (configPath.empty()) {
        return options;
    }

    options.quotePaths = ReadBooleanSetting(configPath, L"quote_paths", options.quotePaths);
    options.folderTrailingSlash = ReadBooleanSetting(configPath, L"folder_trailing_slash", options.folderTrailingSlash);
    options.foldersFirst = ReadBooleanSetting(configPath, L"folders_first", options.foldersFirst);

    wchar_t sortMode[16] = {};
    GetPrivateProfileStringW(L"copy", L"sort_mode", L"explorer", sortMode, _countof(sortMode), configPath.c_str());
    options.explorerSort = _wcsicmp(sortMode, L"none") != 0;
    return options;
}

// 写入带 UTF-16 BOM 的默认配置内容。
bool WriteDefaultConfig(const std::wstring& configPath, DWORD creationDisposition) {
    HANDLE configFile =
        CreateFileW(configPath.c_str(), GENERIC_WRITE, 0, nullptr, creationDisposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (configFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    constexpr wchar_t kUtf16LeBom = 0xFEFF;
    DWORD written = 0;
    const DWORD bomBytes = sizeof(kUtf16LeBom);
    const DWORD textBytes = static_cast<DWORD>(wcslen(kDefaultConfigText) * sizeof(wchar_t));
    const bool success = WriteFile(configFile, &kUtf16LeBom, bomBytes, &written, nullptr) != FALSE &&
                         written == bomBytes &&
                         WriteFile(configFile, kDefaultConfigText, textBytes, &written, nullptr) != FALSE &&
                         written == textBytes;
    CloseHandle(configFile);
    return success;
}

// 创建或覆盖默认配置。
bool InitializeConfig() {
    const std::wstring configPath = GetConfigFilePath();
    if (configPath.empty()) {
        return false;
    }

    const size_t separator = configPath.find_last_of(L'\\');
    const std::wstring configDirectory = configPath.substr(0, separator);
    if (!CreateDirectoryW(configDirectory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }

    return WriteDefaultConfig(configPath, CREATE_ALWAYS);
}

// 获取当前进程所属用户和会话的对象命名空间。
bool GetObjectNames(ObjectNames* names) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    DWORD tokenBytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &tokenBytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        CloseHandle(token);
        return false;
    }

    std::vector<BYTE> tokenBuffer(tokenBytes);
    if (!GetTokenInformation(token, TokenUser, tokenBuffer.data(), tokenBytes, &tokenBytes)) {
        CloseHandle(token);
        return false;
    }
    CloseHandle(token);

    const auto* tokenUser = reinterpret_cast<const TOKEN_USER*>(tokenBuffer.data());
    LPWSTR sidText = nullptr;
    if (!ConvertSidToStringSidW(tokenUser->User.Sid, &sidText)) {
        return false;
    }

    DWORD sessionId = 0;
    const bool hasSession = ProcessIdToSessionId(GetCurrentProcessId(), &sessionId) != FALSE;
    const std::wstring sid(sidText);
    LocalFree(sidText);
    if (!hasSession) {
        return false;
    }

    const std::wstring suffix = sid + L"-" + std::to_wstring(sessionId);
    names->mutex = L"Local\\MultiPathCopy-" + suffix;
    names->mapping = L"Local\\MultiPathCopyData-" + suffix;
    names->readyEvent = L"Local\\MultiPathCopyReady-" + suffix;
    names->changedEvent = L"Local\\MultiPathCopyChanged-" + suffix;
    return true;
}

// 在互斥锁保护下向映射末尾追加一条路径。
bool AppendPath(SharedHeader* header, const std::wstring& path) {
    if (header == nullptr || path.empty()) {
        return false;
    }

    const DWORD pathBytes = static_cast<DWORD>((path.size() + 2) * sizeof(wchar_t));
    if (pathBytes > header->capacityBytes || header->usedBytes > header->capacityBytes - pathBytes) {
        return false;
    }

    auto* data = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(header) + sizeof(SharedHeader));
    const size_t offset = header->usedBytes / sizeof(wchar_t);
    std::copy(path.begin(), path.end(), data + offset);
    data[offset + path.size()] = L'\r';
    data[offset + path.size() + 1] = L'\n';
    header->usedBytes += pathBytes;
    ++header->pathCount;
    return true;
}

// 按用户设置排序并格式化路径，再复制为 Unicode 剪贴板文本。
bool CopyToClipboard(const SharedHeader* header, const CopyOptions& options) {
    if (header == nullptr || header->usedBytes == 0) {
        return false;
    }

    const auto* data = reinterpret_cast<const wchar_t*>(reinterpret_cast<const BYTE*>(header) + sizeof(SharedHeader));
    const size_t characterCount = header->usedBytes / sizeof(wchar_t);
    std::vector<SortablePath> paths;
    for (size_t start = 0; start < characterCount;) {
        size_t end = start;
        while (end < characterCount && data[end] != L'\r' && data[end] != L'\n') {
            ++end;
        }
        if (end > start) {
            std::wstring path(data + start, end - start);
            const DWORD attributes = GetFileAttributesW(path.c_str());
            paths.push_back({std::move(path), attributes != INVALID_FILE_ATTRIBUTES &&
                                             (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
        }
        start = end;
        while (start < characterCount && (data[start] == L'\r' || data[start] == L'\n')) {
            ++start;
        }
    }
    if (paths.empty()) {
        return false;
    }

    if (options.explorerSort) {
        std::sort(paths.begin(), paths.end(), [&options](const SortablePath& left, const SortablePath& right) {
            if (options.foldersFirst && left.isDirectory != right.isDirectory) {
                return left.isDirectory;
            }
            const int comparison = StrCmpLogicalW(left.value.c_str(), right.value.c_str());
            return comparison == 0 ? left.value < right.value : comparison < 0;
        });
    } else if (options.foldersFirst) {
        std::stable_partition(paths.begin(), paths.end(), [](const SortablePath& path) {
            return path.isDirectory;
        });
    }

    std::wstring clipboardText;
    for (const SortablePath& path : paths) {
        std::wstring formattedPath = path.value;
        if (options.folderTrailingSlash && path.isDirectory && !formattedPath.empty() &&
            formattedPath.back() != L'\\') {
            formattedPath += L'\\';
        }
        if (options.quotePaths) {
            clipboardText += L'\"';
        }
        clipboardText += formattedPath;
        if (options.quotePaths) {
            clipboardText += L'\"';
        }
        clipboardText += L"\r\n";
    }

    const size_t bytes = (clipboardText.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr) {
        return false;
    }

    void* destination = GlobalLock(memory);
    if (destination == nullptr) {
        GlobalFree(memory);
        return false;
    }

    CopyMemory(destination, clipboardText.c_str(), bytes);
    GlobalUnlock(memory);

    for (int attempt = 0; attempt < 20; ++attempt) {
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            if (SetClipboardData(CF_UNICODETEXT, memory) != nullptr) {
                CloseClipboard();
                return true;
            }
            CloseClipboard();
            break;
        }
        Sleep(10);
    }

    GlobalFree(memory);
    return false;
}

// 创建当前用户注册表下的右键菜单命令。
bool WriteMenuEntry(HKEY root, const std::wstring& path, const std::wstring& executablePath) {
    HKEY menuKey = nullptr;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &menuKey, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const std::wstring command = L"\"" + executablePath + L"\" \"%1\"";
    const bool success =
        RegSetValueExW(menuKey, L"MUIVerb", 0, REG_SZ, reinterpret_cast<const BYTE*>(kMenuText),
                       static_cast<DWORD>(sizeof(kMenuText))) == ERROR_SUCCESS &&
        RegSetValueExW(menuKey, L"MultiSelectModel", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(L"Player"),
                       static_cast<DWORD>((wcslen(L"Player") + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    RegCloseKey(menuKey);
    if (!success) {
        return false;
    }

    HKEY commandKey = nullptr;
    const std::wstring commandPath = path + L"\\command";
    if (RegCreateKeyExW(root, commandPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &commandKey, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }
    const LONG result = RegSetValueExW(commandKey, nullptr, 0, REG_SZ,
                                       reinterpret_cast<const BYTE*>(command.c_str()),
                                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(commandKey);
    return result == ERROR_SUCCESS;
}

// 安装文件和文件夹的当前用户右键菜单。
bool InstallMenu() {
    wchar_t executablePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, executablePath, MAX_PATH) == 0) {
        return false;
    }

    const std::wstring executable(executablePath);
    const std::wstring base = L"Software\\Classes\\";
    return WriteMenuEntry(HKEY_CURRENT_USER, base + L"*\\shell\\" + kMenuKeyName, executable) &&
           WriteMenuEntry(HKEY_CURRENT_USER, base + L"Directory\\shell\\" + kMenuKeyName, executable);
}

// 删除本工具创建的当前用户右键菜单键。
bool UninstallMenu() {
    const std::wstring base = L"Software\\Classes\\";
    const LONG fileResult = RegDeleteTreeW(HKEY_CURRENT_USER, (base + L"*\\shell\\" + kMenuKeyName).c_str());
    const LONG directoryResult =
        RegDeleteTreeW(HKEY_CURRENT_USER, (base + L"Directory\\shell\\" + kMenuKeyName).c_str());
    return (fileResult == ERROR_SUCCESS || fileResult == ERROR_FILE_NOT_FOUND) &&
           (directoryResult == ERROR_SUCCESS || directoryResult == ERROR_FILE_NOT_FOUND);
}

// 把资源管理器传入的单个路径汇聚到本次剪贴板操作。
int CopySelectedPath(const std::wstring& path) {
    ObjectNames names;
    if (!GetObjectNames(&names)) {
        return 1;
    }

    HANDLE mutex = CreateMutexW(nullptr, FALSE, names.mutex.c_str());
    if (mutex == nullptr) {
        return 1;
    }
    const bool isLeader = GetLastError() != ERROR_ALREADY_EXISTS;

    HANDLE readyEvent = CreateEventW(nullptr, TRUE, FALSE, names.readyEvent.c_str());
    HANDLE changedEvent = CreateEventW(nullptr, FALSE, FALSE, names.changedEvent.c_str());
    if (readyEvent == nullptr || changedEvent == nullptr) {
        if (readyEvent != nullptr) CloseHandle(readyEvent);
        if (changedEvent != nullptr) CloseHandle(changedEvent);
        CloseHandle(mutex);
        return 1;
    }

    HANDLE mapping = nullptr;
    if (isLeader) {
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kMappingSize, names.mapping.c_str());
        if (mapping != nullptr && GetLastError() != ERROR_ALREADY_EXISTS) {
            auto* header = static_cast<SharedHeader*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, kMappingSize));
            if (header != nullptr) {
                ZeroMemory(header, sizeof(SharedHeader));
                CopyMemory(header->magic, kMagic, sizeof(kMagic));
                header->version = kSharedVersion;
                header->capacityBytes = kMappingSize - sizeof(SharedHeader);
                UnmapViewOfFile(header);
            } else {
                CloseHandle(mapping);
                mapping = nullptr;
            }
        }
        if (mapping != nullptr) {
            SetEvent(readyEvent);
        }
    } else {
        if (WaitForSingleObject(readyEvent, kReadyWaitMilliseconds) == WAIT_OBJECT_0) {
            mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, names.mapping.c_str());
        }
    }

    if (mapping == nullptr) {
        CloseHandle(changedEvent);
        CloseHandle(readyEvent);
        CloseHandle(mutex);
        return 1;
    }

    auto* header = static_cast<SharedHeader*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, kMappingSize));
    bool appended = false;
    if (header != nullptr && WaitForSingleObject(mutex, kReadyWaitMilliseconds) == WAIT_OBJECT_0) {
        if (wcscmp(header->magic, kMagic) == 0 && header->version == kSharedVersion) {
            appended = AppendPath(header, path);
        }
        ReleaseMutex(mutex);
    }

    if (!appended) {
        if (header != nullptr) UnmapViewOfFile(header);
        CloseHandle(mapping);
        CloseHandle(changedEvent);
        CloseHandle(readyEvent);
        CloseHandle(mutex);
        return 1;
    }

    if (!isLeader) {
        SetEvent(changedEvent);
    } else {
        while (WaitForSingleObject(changedEvent, kDebounceMilliseconds) == WAIT_OBJECT_0) {
        }
        appended = CopyToClipboard(header, LoadCopyOptions());
    }

    UnmapViewOfFile(header);
    CloseHandle(mapping);
    CloseHandle(changedEvent);
    CloseHandle(readyEvent);
    CloseHandle(mutex);
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr) {
        ShowCommandResult(false, L"无法读取命令行参数。");
        return 1;
    }

    int result = 1;
    if (argumentCount == 1 ||
        (argumentCount == 2 && (wcscmp(arguments[1], L"--help") == 0 || wcscmp(arguments[1], L"-h") == 0))) {
        ShowUsage();
        result = 0;
    } else if (argumentCount == 2 && wcscmp(arguments[1], L"--install") == 0) {
        result = InstallMenu() ? 0 : 1;
        ShowCommandResult(result == 0, result == 0 ? L"已安装文件和文件夹的右键菜单。"
                                                    : L"无法安装右键菜单。请确认当前用户注册表可写。");
    } else if (argumentCount == 2 && wcscmp(arguments[1], L"--uninstall") == 0) {
        result = UninstallMenu() ? 0 : 1;
        ShowCommandResult(result == 0, result == 0 ? L"已卸载本工具创建的右键菜单。"
                                                    : L"无法卸载右键菜单。请稍后重试。");
    } else if (argumentCount == 2 && wcscmp(arguments[1], L"--init-config") == 0) {
        const std::wstring configPath = GetConfigFilePath();
        if (InitializeConfig() && !configPath.empty()) {
            ShowCommandResult(true, L"已创建默认配置文件：\n" + configPath);
            result = 0;
        } else {
            ShowCommandResult(false, L"无法创建默认配置文件。请确认 %LOCALAPPDATA% 可写。");
        }
    } else if (argumentCount == 2 && wcscmp(arguments[1], L"--config") == 0) {
        const std::wstring configPath = GetConfigFilePath();
        if (!configPath.empty()) {
            ShowCommandResult(true, L"配置文件位置：\n" + configPath);
            result = 0;
        } else {
            ShowCommandResult(false, L"无法确定配置文件位置。");
        }
    } else if (argumentCount == 2) {
        result = CopySelectedPath(arguments[1]);
        ShowCommandResult(result == 0, result == 0 ? L"已复制路径到剪贴板。" : L"无法复制路径到剪贴板。");
    } else {
        ShowCommandResult(false, L"无法识别命令行参数。");
        ShowUsage();
    }

    LocalFree(arguments);
    return result;
}
