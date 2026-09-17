/*
 * BALDR FORCE EXE Win11 compatibility launcher
 * Copyright (c) 2026 BALDR FORCE EXE Win11 Fix contributors
 * SPDX-License-Identifier: MIT
 *
 * This launcher contains no game executable, translation, or game asset.
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <wchar.h>
#include <wctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "game_patch.h"
#include "launcher_config.h"
#include "launcher_ui.h"

#define GAME_IMAGE_SIGNATURE_ADDRESS ((LPCVOID)(uintptr_t)0x00461108)
#define FIND_FIRST_FILE_IAT_ADDRESS   ((LPCVOID)(uintptr_t)0x004BD0F0)
#define RESOURCE_NAME_ADDRESS         ((LPVOID)(uintptr_t)0x004DD190)
#define PATCH_TIMEOUT_MS              10000
#define WINDOW_TIMEOUT_MS             30000
#define EXPECTED_GAME_SIZE            46252058ULL
#define ALIAS_MARKER                  L"Sx\\.baldrforce-se-alias"

static HANDLE log_file = INVALID_HANDLE_VALUE;

static void write_log(const wchar_t *stage, DWORD error_code)
{
    SYSTEMTIME now;
    wchar_t line[512];
    char utf8[1536];
    int characters;
    int bytes;
    DWORD written;

    if (log_file == INVALID_HANDLE_VALUE) {
        return;
    }
    GetLocalTime(&now);
    characters = _snwprintf(line, sizeof(line) / sizeof(line[0]) - 1,
                            L"%04u-%02u-%02u %02u:%02u:%02u [%ls] error=%lu\r\n",
                            now.wYear, now.wMonth, now.wDay,
                            now.wHour, now.wMinute, now.wSecond,
                            stage, (unsigned long)error_code);
    if (characters < 0) {
        return;
    }
    line[sizeof(line) / sizeof(line[0]) - 1] = L'\0';
    bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8,
                                (int)sizeof(utf8), NULL, NULL);
    if (bytes > 1) {
        WriteFile(log_file, utf8, (DWORD)(bytes - 1), &written, NULL);
        FlushFileBuffers(log_file);
    }
}

static void open_log(void)
{
    log_file = CreateFileW(L"Start-BaldrForce.log", FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    write_log(L"launcher-start", ERROR_SUCCESS);
}

static void close_log(void)
{
    if (log_file != INVALID_HANDLE_VALUE) {
        write_log(L"launcher-exit", ERROR_SUCCESS);
        CloseHandle(log_file);
        log_file = INVALID_HANDLE_VALUE;
    }
}

static void show_error(const wchar_t *message, DWORD error_code)
{
    wchar_t detail[1024];

    if (error_code != ERROR_SUCCESS) {
        wchar_t system_message[512] = L"";
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, error_code, 0, system_message,
                       (DWORD)(sizeof(system_message) / sizeof(system_message[0])), NULL);
        _snwprintf(detail, sizeof(detail) / sizeof(detail[0]) - 1,
                   L"%ls\n\nWindows 错误 %lu：%ls",
                   message, (unsigned long)error_code, system_message);
        detail[sizeof(detail) / sizeof(detail[0]) - 1] = L'\0';
    } else {
        wcsncpy(detail, message, sizeof(detail) / sizeof(detail[0]) - 1);
        detail[sizeof(detail) / sizeof(detail[0]) - 1] = L'\0';
    }

    MessageBoxW(NULL, detail, L"BALDR FORCE EXE 中文版启动器", MB_OK | MB_ICONERROR);
}

static void show_launch_error(const wchar_t *stage, const wchar_t *message,
                              DWORD error_code)
{
    write_log(stage, error_code);
    show_error(message, error_code);
}

static BOOL read_exact(HANDLE process, LPCVOID address, void *buffer, SIZE_T size)
{
    SIZE_T read = 0;
    return ReadProcessMemory(process, address, buffer, size, &read) && read == size;
}

static BOOL set_working_directory_to_launcher(void)
{
    wchar_t path[MAX_PATH];
    wchar_t *separator;

    if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0) {
        return FALSE;
    }
    path[MAX_PATH - 1] = L'\0';
    separator = wcsrchr(path, L'\\');
    if (separator == NULL) {
        SetLastError(ERROR_BAD_PATHNAME);
        return FALSE;
    }
    *separator = L'\0';
    return SetCurrentDirectoryW(path);
}

static BOOL game_files_available(wchar_t *status, size_t status_count)
{
    WIN32_FILE_ATTRIBUTE_DATA game_data;
    ULARGE_INTEGER game_size;

    if (!GetFileAttributesExW(L"BaldrForce.exe", GetFileExInfoStandard, &game_data) ||
        (game_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        wcsncpy(status, L"未找到 BaldrForce.exe。请把启动器与补丁文件放入游戏目录。",
                status_count - 1);
        status[status_count - 1] = L'\0';
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    game_size.HighPart = game_data.nFileSizeHigh;
    game_size.LowPart = game_data.nFileSizeLow;
    if (game_size.QuadPart != EXPECTED_GAME_SIZE) {
        _snwprintf(status, status_count - 1,
                   L"游戏程序大小不匹配：当前 %llu 字节，支持版本为 %llu 字节。",
                   (unsigned long long)game_size.QuadPart,
                   (unsigned long long)EXPECTED_GAME_SIZE);
        status[status_count - 1] = L'\0';
        SetLastError(ERROR_BAD_EXE_FORMAT);
        return FALSE;
    }
    if (GetFileAttributesW(L"ddraw.dll") == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(DGVOODOO_CONFIG_FILE) == INVALID_FILE_ATTRIBUTES) {
        wcsncpy(status, L"缺少 ddraw.dll 或 dgVoodoo.conf，请重新解压完整补丁。",
                status_count - 1);
        status[status_count - 1] = L'\0';
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    if (GetFileAttributesW(L"Se\\Abort.wav") == INVALID_FILE_ATTRIBUTES) {
        wcsncpy(status, L"音效资源目录 Se 不存在或不完整。",
                status_count - 1);
        status[status_count - 1] = L'\0';
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    status[0] = L'\0';
    return TRUE;
}

static BOOL get_game_full_path(wchar_t *path, DWORD path_count)
{
    DWORD length = GetFullPathNameW(L"BaldrForce.exe", path_count, path, NULL);
    if (length == 0 || length >= path_count) {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    return TRUE;
}

typedef enum GameInstanceStatus {
    GAME_INSTANCE_NOT_RUNNING,
    GAME_INSTANCE_RUNNING,
    GAME_INSTANCE_SCAN_FAILED
} GameInstanceStatus;

static GameInstanceStatus get_game_instance_status(void)
{
    wchar_t expected_path[32768];
    PROCESSENTRY32W entry;
    HANDLE snapshot;
    DWORD error_code;

    if (!get_game_full_path(expected_path,
                            (DWORD)(sizeof(expected_path) / sizeof(expected_path[0])))) {
        return GAME_INSTANCE_SCAN_FAILED;
    }
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return GAME_INSTANCE_SCAN_FAILED;
    }
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        error_code = GetLastError();
        CloseHandle(snapshot);
        SetLastError(error_code);
        return GAME_INSTANCE_SCAN_FAILED;
    }
    do {
        if (_wcsicmp(entry.szExeFile, L"BaldrForce.exe") == 0) {
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                         FALSE, entry.th32ProcessID);
            wchar_t process_path[32768];
            DWORD process_path_count = (DWORD)
                (sizeof(process_path) / sizeof(process_path[0]));
            BOOL queried;

            if (process == NULL) {
                DWORD open_error = GetLastError();
                if (open_error == ERROR_INVALID_PARAMETER) {
                    continue;
                }
                error_code = open_error;
                CloseHandle(snapshot);
                SetLastError(error_code);
                return GAME_INSTANCE_SCAN_FAILED;
            }
            queried = QueryFullProcessImageNameW(process, 0, process_path,
                                                  &process_path_count);
            error_code = queried ? ERROR_SUCCESS : GetLastError();
            CloseHandle(process);
            if (!queried) {
                if (error_code == ERROR_INVALID_PARAMETER ||
                    error_code == ERROR_NOT_FOUND) {
                    continue;
                }
                CloseHandle(snapshot);
                SetLastError(error_code);
                return GAME_INSTANCE_SCAN_FAILED;
            }
            if (_wcsicmp(process_path, expected_path) == 0) {
                CloseHandle(snapshot);
                return GAME_INSTANCE_RUNNING;
            }
        }
    } while (Process32NextW(snapshot, &entry));
    error_code = GetLastError();
    CloseHandle(snapshot);
    if (error_code != ERROR_NO_MORE_FILES) {
        SetLastError(error_code);
        return GAME_INSTANCE_SCAN_FAILED;
    }
    return GAME_INSTANCE_NOT_RUNNING;
}

static HANDLE acquire_launch_mutex(void)
{
    wchar_t game_path[32768];
    wchar_t mutex_name[96];
    uint64_t path_hash = UINT64_C(14695981039346656037);
    size_t index;
    HANDLE mutex;
    DWORD wait_result;

    if (!get_game_full_path(game_path,
                            (DWORD)(sizeof(game_path) / sizeof(game_path[0])))) {
        return NULL;
    }
    for (index = 0; game_path[index] != L'\0'; ++index) {
        wchar_t character = towlower(game_path[index]);
        path_hash ^= (uint64_t)character;
        path_hash *= UINT64_C(1099511628211);
    }
    _snwprintf(mutex_name, sizeof(mutex_name) / sizeof(mutex_name[0]) - 1,
               L"Local\\BaldrForceEXE-Win11-Fix-%016llX",
               (unsigned long long)path_hash);
    mutex_name[sizeof(mutex_name) / sizeof(mutex_name[0]) - 1] = L'\0';
    mutex = CreateMutexW(NULL, FALSE, mutex_name);
    if (mutex == NULL) {
        return NULL;
    }
    wait_result = WaitForSingleObject(mutex, 0);
    if (wait_result != WAIT_OBJECT_0 && wait_result != WAIT_ABANDONED) {
        DWORD error_code = wait_result == WAIT_TIMEOUT
                               ? ERROR_ALREADY_EXISTS : GetLastError();
        CloseHandle(mutex);
        SetLastError(error_code);
        return NULL;
    }
    return mutex;
}

static void release_launch_mutex(HANDLE mutex)
{
    if (mutex != NULL) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
}

static BOOL ensure_sound_alias(void)
{
    WIN32_FIND_DATAW source_data;
    WIN32_FILE_ATTRIBUTE_DATA target_data;
    HANDLE find;
    DWORD attributes;
    BOOL created_directory = FALSE;
    unsigned int file_count = 0;
    wchar_t source_path[MAX_PATH];
    wchar_t target_path[MAX_PATH];

    attributes = GetFileAttributesW(L"Se");
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }

    attributes = GetFileAttributesW(L"Sx");
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryW(L"Sx", NULL)) {
            return FALSE;
        }
        created_directory = TRUE;
    } else if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
               (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        SetLastError(ERROR_ALREADY_EXISTS);
        return FALSE;
    }

    attributes = GetFileAttributesW(ALIAS_MARKER);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (!created_directory) {
            SetLastError(ERROR_ALREADY_EXISTS);
            return FALSE;
        }
        if (!CreateDirectoryW(ALIAS_MARKER, NULL) ||
            !SetFileAttributesW(ALIAS_MARKER, FILE_ATTRIBUTE_HIDDEN)) {
            return FALSE;
        }
    } else if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        SetLastError(ERROR_ALREADY_EXISTS);
        return FALSE;
    }

    find = FindFirstFileW(L"Se\\*", &source_data);
    if (find == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    do {
        ULARGE_INTEGER source_size;
        ULARGE_INTEGER target_size;

        if ((source_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            wcscmp(source_data.cFileName, L".") == 0 ||
            wcscmp(source_data.cFileName, L"..") == 0) {
            continue;
        }

        if (_snwprintf(source_path, MAX_PATH - 1, L"Se\\%ls", source_data.cFileName) < 0 ||
            _snwprintf(target_path, MAX_PATH - 1, L"Sx\\%ls", source_data.cFileName) < 0) {
            FindClose(find);
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return FALSE;
        }
        source_path[MAX_PATH - 1] = L'\0';
        target_path[MAX_PATH - 1] = L'\0';

        source_size.HighPart = source_data.nFileSizeHigh;
        source_size.LowPart = source_data.nFileSizeLow;

        if (GetFileAttributesExW(target_path, GetFileExInfoStandard, &target_data)) {
            target_size.HighPart = target_data.nFileSizeHigh;
            target_size.LowPart = target_data.nFileSizeLow;
            if (!(target_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                target_size.QuadPart == source_size.QuadPart) {
                ++file_count;
                continue;
            }
            if (!DeleteFileW(target_path)) {
                FindClose(find);
                return FALSE;
            }
        }

        if (!CreateHardLinkW(target_path, source_path, NULL) &&
            !CopyFileW(source_path, target_path, FALSE)) {
            FindClose(find);
            return FALSE;
        }
        ++file_count;
    } while (FindNextFileW(find, &source_data));

    if (GetLastError() != ERROR_NO_MORE_FILES) {
        DWORD error_code = GetLastError();
        FindClose(find);
        SetLastError(error_code);
        return FALSE;
    }
    FindClose(find);

    if (file_count == 0 || GetFileAttributesW(L"Sx\\Abort.wav") == INVALID_FILE_ATTRIBUTES) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    return TRUE;
}

static BOOL wait_for_unpacked_image(HANDLE process)
{
    static const BYTE expected_code[] = { 0x8D, 0x7E, 0x04, 0x8D, 0x94, 0x24, 0x24, 0x02 };
    static const BYTE expected_name[] = { 'S', 'e', 0x00 };
    BYTE code[sizeof(expected_code)];
    BYTE name[sizeof(expected_name)];
    uint32_t find_first_file = 0;
    DWORD started = GetTickCount();

    while (GetTickCount() - started < PATCH_TIMEOUT_MS) {
        DWORD exit_code = STILL_ACTIVE;

        if (!GetExitCodeProcess(process, &exit_code) || exit_code != STILL_ACTIVE) {
            SetLastError(ERROR_PROCESS_ABORTED);
            return FALSE;
        }

        if (read_exact(process, GAME_IMAGE_SIGNATURE_ADDRESS, code, sizeof(code)) &&
            read_exact(process, RESOURCE_NAME_ADDRESS, name, sizeof(name)) &&
            read_exact(process, FIND_FIRST_FILE_IAT_ADDRESS,
                       &find_first_file, sizeof(find_first_file)) &&
            memcmp(code, expected_code, sizeof(code)) == 0 &&
            memcmp(name, expected_name, sizeof(name)) == 0 &&
            find_first_file > 0x007E0000U) {
            return TRUE;
        }
        Sleep(0);
    }

    SetLastError(WAIT_TIMEOUT);
    return FALSE;
}

static BOOL verify_patch_target(HANDLE process)
{
    static const BYTE expected_code[] = { 0x8D, 0x7E, 0x04, 0x8D, 0x94, 0x24, 0x24, 0x02 };
    static const BYTE expected_name[] = { 'S', 'e', 0x00 };
    BYTE code[sizeof(expected_code)];
    BYTE name[sizeof(expected_name)];

    return read_exact(process, GAME_IMAGE_SIGNATURE_ADDRESS, code, sizeof(code)) &&
           read_exact(process, RESOURCE_NAME_ADDRESS, name, sizeof(name)) &&
           memcmp(code, expected_code, sizeof(code)) == 0 &&
           memcmp(name, expected_name, sizeof(name)) == 0;
}

typedef struct WindowSearch {
    DWORD process_id;
    HWND best_window;
    LONG best_area;
} WindowSearch;

static BOOL CALLBACK find_game_window_callback(HWND window, LPARAM parameter)
{
    WindowSearch *search = (WindowSearch *)parameter;
    DWORD process_id = 0;
    RECT client;
    LONG width;
    LONG height;
    LONG area;

    GetWindowThreadProcessId(window, &process_id);
    if (process_id != search->process_id || !IsWindowVisible(window) ||
        !GetClientRect(window, &client)) {
        return TRUE;
    }
    width = client.right - client.left;
    height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        return TRUE;
    }
    area = width * height;
    if (area > search->best_area) {
        search->best_area = area;
        search->best_window = window;
    }
    return TRUE;
}

static HWND wait_for_game_window(HANDLE process, DWORD process_id)
{
    DWORD started = GetTickCount();

    while (GetTickCount() - started < WINDOW_TIMEOUT_MS) {
        DWORD exit_code = STILL_ACTIVE;
        WindowSearch search;
        if (!GetExitCodeProcess(process, &exit_code) || exit_code != STILL_ACTIVE) {
            SetLastError(ERROR_PROCESS_ABORTED);
            return NULL;
        }
        ZeroMemory(&search, sizeof(search));
        search.process_id = process_id;
        EnumWindows(find_game_window_callback, (LPARAM)&search);
        if (search.best_window != NULL) {
            return search.best_window;
        }
        Sleep(50);
    }
    SetLastError(WAIT_TIMEOUT);
    return NULL;
}

static BOOL set_game_display(HANDLE process, DWORD process_id,
                             const LauncherSettings *settings)
{
    HWND window;
    HMONITOR monitor;
    MONITORINFO monitor_info;
    LONG_PTR current_style;
    LONG_PTR current_extended_style;
    LONG_PTR desired_style;
    LONG_PTR desired_extended_style;
    RECT rectangle;
    int x;
    int y;
    int width;
    int height;
    UINT flags = SWP_FRAMECHANGED | SWP_SHOWWINDOW;

    if (settings->display_mode == DISPLAY_MODE_EXCLUSIVE) {
        return TRUE;
    }
    window = wait_for_game_window(process, process_id);
    if (window == NULL) {
        return FALSE;
    }
    monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    ZeroMemory(&monitor_info, sizeof(monitor_info));
    monitor_info.cbSize = sizeof(monitor_info);
    if (!GetMonitorInfoW(monitor, &monitor_info)) {
        return FALSE;
    }
    current_style = GetWindowLongPtrW(window, GWL_STYLE);
    current_extended_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    desired_extended_style = current_extended_style &
        ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);

    if (settings->display_mode == DISPLAY_MODE_BORDERLESS) {
        desired_style = (current_style & (WS_CLIPCHILDREN | WS_CLIPSIBLINGS)) |
                        WS_POPUP | WS_VISIBLE;
        rectangle = monitor_info.rcMonitor;
        x = rectangle.left;
        y = rectangle.top;
        width = rectangle.right - rectangle.left;
        height = rectangle.bottom - rectangle.top;
    } else {
        UINT dpi;
        RECT desired_client = { 0, 0, (LONG)settings->width, (LONG)settings->height };
        desired_style = (current_style & (WS_CLIPCHILDREN | WS_CLIPSIBLINGS)) |
                        WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        dpi = GetDpiForWindow(window);
        if (dpi == 0) {
            dpi = 96;
        }
        if (!AdjustWindowRectExForDpi(&desired_client, (DWORD)desired_style,
                                      FALSE, (DWORD)desired_extended_style, dpi)) {
            return FALSE;
        }
        width = desired_client.right - desired_client.left;
        height = desired_client.bottom - desired_client.top;
        if (width > monitor_info.rcWork.right - monitor_info.rcWork.left ||
            height > monitor_info.rcWork.bottom - monitor_info.rcWork.top) {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
        if (settings->center_window) {
            x = monitor_info.rcWork.left +
                ((monitor_info.rcWork.right - monitor_info.rcWork.left) - width) / 2;
            y = monitor_info.rcWork.top +
                ((monitor_info.rcWork.bottom - monitor_info.rcWork.top) - height) / 2;
        } else {
            x = monitor_info.rcWork.left;
            y = monitor_info.rcWork.top;
        }
    }
    SetLastError(ERROR_SUCCESS);
    if (SetWindowLongPtrW(window, GWL_STYLE, desired_style) == 0 &&
        GetLastError() != ERROR_SUCCESS) {
        return FALSE;
    }
    SetLastError(ERROR_SUCCESS);
    if (SetWindowLongPtrW(window, GWL_EXSTYLE, desired_extended_style) == 0 &&
        GetLastError() != ERROR_SUCCESS) {
        return FALSE;
    }
    return SetWindowPos(window, HWND_TOP, x, y, width, height, flags);
}

static RequestedAction parse_requested_action(void)
{
    int argument_count = 0;
    LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    RequestedAction action;

    if (arguments == NULL) {
        return ACTION_INVALID;
    }
    action = launcher_requested_action_parse(
        argument_count, argument_count == 2 ? arguments[1] : NULL);
    LocalFree(arguments);
    return action;
}

typedef struct LaunchExecutionContext {
    int result;
    BOOL launched;
} LaunchExecutionContext;

static BOOL suspend_secondary_threads(DWORD process_id, DWORD primary_thread_id,
                                      HANDLE **threads, size_t *thread_count)
{
    HANDLE snapshot;
    HANDLE *suspended = NULL;
    size_t count = 0;
    size_t capacity = 0;
    THREADENTRY32 entry;
    BOOL has_entry;
    DWORD error_code = ERROR_SUCCESS;

    if (threads == NULL || thread_count == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *threads = NULL;
    *thread_count = 0;
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    has_entry = Thread32First(snapshot, &entry);
    if (!has_entry && GetLastError() != ERROR_NO_MORE_FILES) {
        error_code = GetLastError();
        goto fail;
    }
    while (has_entry) {
        if (entry.th32OwnerProcessID == process_id &&
            entry.th32ThreadID != primary_thread_id) {
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME |
                                           THREAD_QUERY_LIMITED_INFORMATION,
                                       FALSE,
                                       entry.th32ThreadID);
            HANDLE *expanded;
            DWORD owner_process_id;

            if (thread == NULL) {
                error_code = GetLastError();
                if (error_code == ERROR_INVALID_PARAMETER) {
                    has_entry = Thread32Next(snapshot, &entry);
                    continue;
                }
                goto fail;
            }
            owner_process_id = GetProcessIdOfThread(thread);
            if (owner_process_id == 0) {
                error_code = GetLastError();
                CloseHandle(thread);
                if (error_code == ERROR_INVALID_PARAMETER) {
                    has_entry = Thread32Next(snapshot, &entry);
                    continue;
                }
                goto fail;
            }
            if (owner_process_id != process_id) {
                CloseHandle(thread);
                has_entry = Thread32Next(snapshot, &entry);
                continue;
            }
            if (SuspendThread(thread) == (DWORD)-1) {
                error_code = GetLastError();
                CloseHandle(thread);
                goto fail;
            }
            if (count == capacity) {
                size_t next_capacity = capacity == 0 ? 8U : capacity * 2U;
                expanded = (HANDLE *)realloc(suspended,
                                             next_capacity * sizeof(*expanded));
                if (expanded == NULL) {
                    error_code = ERROR_OUTOFMEMORY;
                    ResumeThread(thread);
                    CloseHandle(thread);
                    goto fail;
                }
                suspended = expanded;
                capacity = next_capacity;
            }
            suspended[count++] = thread;
        }
        has_entry = Thread32Next(snapshot, &entry);
        if (!has_entry && GetLastError() != ERROR_NO_MORE_FILES) {
            error_code = GetLastError();
            goto fail;
        }
    }
    CloseHandle(snapshot);
    *threads = suspended;
    *thread_count = count;
    SetLastError(ERROR_SUCCESS);
    return TRUE;

fail:
    CloseHandle(snapshot);
    while (count != 0) {
        --count;
        ResumeThread(suspended[count]);
        CloseHandle(suspended[count]);
    }
    free(suspended);
    SetLastError(error_code);
    return FALSE;
}

static void close_secondary_threads(HANDLE *threads, size_t thread_count)
{
    while (thread_count != 0) {
        --thread_count;
        CloseHandle(threads[thread_count]);
    }
    free(threads);
}

static BOOL resume_secondary_threads(HANDLE *threads, size_t thread_count)
{
    DWORD error_code = ERROR_SUCCESS;

    while (thread_count != 0) {
        --thread_count;
        if (ResumeThread(threads[thread_count]) == (DWORD)-1 &&
            error_code == ERROR_SUCCESS) {
            error_code = GetLastError();
        }
        CloseHandle(threads[thread_count]);
    }
    free(threads);
    if (error_code != ERROR_SUCCESS) {
        SetLastError(error_code);
        return FALSE;
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static BOOL terminate_process_and_wait(HANDLE process, BOOL *termination_started)
{
    DWORD wait_result;
    DWORD error_code;
    DWORD exit_code;

    if (process == NULL || process == INVALID_HANDLE_VALUE ||
        termination_started == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *termination_started = FALSE;
    wait_result = WaitForSingleObject(process, 0);
    if (wait_result == WAIT_OBJECT_0) {
        *termination_started = TRUE;
        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }
    if (wait_result == WAIT_FAILED) {
        return FALSE;
    }
    if (!TerminateProcess(process, 1)) {
        error_code = GetLastError();
        if (!GetExitCodeProcess(process, &exit_code) || exit_code == STILL_ACTIVE) {
            SetLastError(error_code);
            return FALSE;
        }
    }
    *termination_started = TRUE;
    wait_result = WaitForSingleObject(process, 5000);
    if (wait_result == WAIT_OBJECT_0) {
        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }
    if (wait_result == WAIT_TIMEOUT) {
        SetLastError(ERROR_TIMEOUT);
    }
    return FALSE;
}

static void terminate_failed_launch(HANDLE process, HANDLE primary_thread,
                                    BOOL *primary_thread_suspended,
                                    HANDLE **secondary_threads,
                                    size_t *secondary_thread_count)
{
    BOOL termination_started = FALSE;
    DWORD termination_error = ERROR_SUCCESS;

    if (!terminate_process_and_wait(process, &termination_started)) {
        termination_error = GetLastError();
        write_log(L"terminate-failed-launch", termination_error);
    }
    if (*secondary_threads != NULL) {
        if (termination_started) {
            close_secondary_threads(*secondary_threads,
                                    *secondary_thread_count);
        } else if (!resume_secondary_threads(*secondary_threads,
                                             *secondary_thread_count)) {
            write_log(L"resume-secondary-after-terminate-failure",
                      GetLastError());
        }
        *secondary_threads = NULL;
        *secondary_thread_count = 0;
    }
    if (!termination_started && *primary_thread_suspended) {
        if (ResumeThread(primary_thread) == (DWORD)-1) {
            write_log(L"resume-primary-after-terminate-failure",
                      GetLastError());
        } else {
            *primary_thread_suspended = FALSE;
        }
    }
}

static BOOL launch_game_with_settings(const LauncherSettings *settings, void *user_data)
{
    static const BYTE resource_alias[] = { 'S', 'x' };
    LaunchExecutionContext *execution = (LaunchExecutionContext *)user_data;
    STARTUPINFOW startup;
    PROCESS_INFORMATION process_info;
    wchar_t game_command[] = L"\"BaldrForce.exe\"";
    wchar_t preflight_status[512] = L"";
    wchar_t settings_error[512] = L"";
    HANDLE launch_mutex = NULL;
    HANDLE *secondary_threads = NULL;
    size_t secondary_thread_count = 0;
    BOOL primary_thread_suspended = FALSE;
    GameInstanceStatus instance_status;
    SIZE_T written = 0;
    DWORD error_code;

    execution->launched = FALSE;
    open_log();
    if (!game_files_available(preflight_status,
                              sizeof(preflight_status) / sizeof(preflight_status[0]))) {
        error_code = GetLastError();
        show_launch_error(L"preflight", preflight_status, error_code);
        execution->result = 2;
        goto cleanup;
    }

    launch_mutex = acquire_launch_mutex();
    if (launch_mutex == NULL) {
        error_code = GetLastError();
        show_launch_error(L"instance-lock",
                          error_code == ERROR_ALREADY_EXISTS
                              ? L"该目录的另一个启动器正在启动游戏，请稍候。"
                              : L"无法建立同目录启动互斥保护，已停止启动以避免配置冲突。",
                          error_code);
        execution->result = 3;
        goto cleanup;
    }
    instance_status = get_game_instance_status();
    if (instance_status != GAME_INSTANCE_NOT_RUNNING) {
        error_code = instance_status == GAME_INSTANCE_RUNNING
                         ? ERROR_ALREADY_EXISTS : GetLastError();
        show_launch_error(instance_status == GAME_INSTANCE_RUNNING
                              ? L"instance-running" : L"instance-scan",
                          instance_status == GAME_INSTANCE_RUNNING
                              ? L"该目录中的游戏已在运行。请先关闭现有游戏窗口。"
                              : L"无法可靠确认该目录中是否已有游戏实例；已安全停止启动。",
                          error_code);
        execution->result = 3;
        goto cleanup;
    }

    if (!dgvoodoo_config_apply(DGVOODOO_CONFIG_FILE, DGVOODOO_BACKUP_FILE,
                               settings, settings_error,
                               sizeof(settings_error) / sizeof(settings_error[0]))) {
        error_code = GetLastError();
        show_launch_error(L"dgvoodoo-config", settings_error, error_code);
        execution->result = 4;
        goto cleanup;
    }
    if (!launcher_settings_save(LAUNCHER_SETTINGS_FILE, settings, settings_error,
                                sizeof(settings_error) / sizeof(settings_error[0]))) {
        error_code = GetLastError();
        show_launch_error(L"launcher-settings", settings_error, error_code);
        execution->result = 5;
        goto cleanup;
    }
    if (!ensure_sound_alias()) {
        error_code = GetLastError();
        show_launch_error(L"sound-alias",
                          L"无法建立完整的 Sx 音效资源别名目录。", error_code);
        execution->result = 6;
        goto cleanup;
    }

    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process_info, sizeof(process_info));
    startup.cb = sizeof(startup);
    if (!CreateProcessW(NULL, game_command, NULL, NULL, FALSE, CREATE_SUSPENDED,
                        NULL, NULL, &startup, &process_info)) {
        error_code = GetLastError();
        show_launch_error(L"create-process", L"无法启动 BaldrForce.exe。", error_code);
        execution->result = 7;
        goto cleanup;
    }
    primary_thread_suspended = TRUE;

    if (ResumeThread(process_info.hThread) == (DWORD)-1) {
        error_code = GetLastError();
        terminate_failed_launch(process_info.hProcess, process_info.hThread,
                                &primary_thread_suspended, &secondary_threads,
                                &secondary_thread_count);
        show_launch_error(L"resume-unpacker", L"无法恢复游戏启动线程。", error_code);
        execution->result = 8;
        goto cleanup_process;
    }
    primary_thread_suspended = FALSE;
    if (!wait_for_unpacked_image(process_info.hProcess)) {
        error_code = GetLastError();
        terminate_failed_launch(process_info.hProcess, process_info.hThread,
                                &primary_thread_suspended, &secondary_threads,
                                &secondary_thread_count);
        show_launch_error(L"wait-unpacker",
                          L"等待汉化封装器解包超时，未应用资源路径修复。", error_code);
        execution->result = 9;
        goto cleanup_process;
    }
    if (SuspendThread(process_info.hThread) == (DWORD)-1) {
        error_code = GetLastError();
        terminate_failed_launch(process_info.hProcess, process_info.hThread,
                                &primary_thread_suspended, &secondary_threads,
                                &secondary_thread_count);
        show_launch_error(L"suspend-patch",
                          L"无法暂停游戏以应用 Se 到 Sx 的资源路径修复。", error_code);
        execution->result = 10;
        goto cleanup_process;
    }
    primary_thread_suspended = TRUE;
    if (!suspend_secondary_threads(process_info.dwProcessId,
                                   process_info.dwThreadId,
                                   &secondary_threads,
                                   &secondary_thread_count)) {
        error_code = GetLastError();
        terminate_failed_launch(process_info.hProcess, process_info.hThread,
                                &primary_thread_suspended, &secondary_threads,
                                &secondary_thread_count);
        show_launch_error(L"suspend-secondary-threads",
                          L"无法暂停游戏当前的辅助线程以安全应用进程补丁。",
                          error_code);
        execution->result = 10;
        goto cleanup_process;
    }
    if (!verify_patch_target(process_info.hProcess)) {
        error_code = GetLastError();
        if (error_code == ERROR_SUCCESS) {
            error_code = ERROR_INVALID_DATA;
        }
        terminate_failed_launch(process_info.hProcess, process_info.hThread,
                                &primary_thread_suspended, &secondary_threads,
                                &secondary_thread_count);
        show_launch_error(L"verify-patch-target",
                          L"游戏内补丁目标与支持版本不一致，未写入进程。", error_code);
        execution->result = 11;
        goto cleanup_process;
    }
    if (!WriteProcessMemory(process_info.hProcess, RESOURCE_NAME_ADDRESS,
                            resource_alias, sizeof(resource_alias), &written) ||
        written != sizeof(resource_alias) ||
        !FlushInstructionCache(process_info.hProcess, RESOURCE_NAME_ADDRESS,
                               sizeof(resource_alias))) {
        error_code = GetLastError();
        terminate_failed_launch(process_info.hProcess, process_info.hThread,
                                &primary_thread_suspended, &secondary_threads,
                                &secondary_thread_count);
        show_launch_error(L"write-resource-alias",
                          L"无法应用 Se 到 Sx 的资源路径修复。", error_code);
        execution->result = 11;
        goto cleanup_process;
    }
    if (settings->display_mode != DISPLAY_MODE_EXCLUSIVE &&
        !game_desktop_display_patch_apply(process_info.hProcess)) {
        error_code = GetLastError();
        terminate_failed_launch(process_info.hProcess, process_info.hThread,
                                &primary_thread_suspended, &secondary_threads,
                                &secondary_thread_count);
        show_launch_error(L"patch-desktop-display",
                          L"无法应用桌面显示模式的 DirectDraw 生命周期修复。",
                          error_code);
        execution->result = 12;
        goto cleanup_process;
    }
    if (settings->display_mode != DISPLAY_MODE_EXCLUSIVE &&
        !game_inactive_input_patch_apply(process_info.hProcess)) {
        error_code = GetLastError();
        terminate_failed_launch(process_info.hProcess, process_info.hThread,
                                &primary_thread_suspended, &secondary_threads,
                                &secondary_thread_count);
        show_launch_error(L"patch-inactive-input",
                          L"无法应用失焦键盘和鼠标输入过滤修复。",
                          error_code);
        execution->result = 13;
        goto cleanup_process;
    }
    if (settings->display_mode == DISPLAY_MODE_WINDOWED &&
        !game_windowed_mouse_patch_apply(process_info.hProcess,
                                         settings->width, settings->height,
                                         settings->keep_aspect_ratio)) {
        error_code = GetLastError();
        terminate_failed_launch(process_info.hProcess, process_info.hThread,
                                &primary_thread_suspended, &secondary_threads,
                                &secondary_thread_count);
        show_launch_error(L"patch-windowed-mouse",
                          L"无法应用窗口模式鼠标坐标修复。", error_code);
        execution->result = 13;
        goto cleanup_process;
    }
    if (!resume_secondary_threads(secondary_threads, secondary_thread_count)) {
        error_code = GetLastError();
        secondary_threads = NULL;
        secondary_thread_count = 0;
        terminate_failed_launch(process_info.hProcess, process_info.hThread,
                                &primary_thread_suspended, &secondary_threads,
                                &secondary_thread_count);
        show_launch_error(L"resume-secondary-threads",
                          L"应用进程补丁后无法恢复游戏的全部线程。",
                          error_code);
        execution->result = 14;
        goto cleanup_process;
    }
    secondary_threads = NULL;
    secondary_thread_count = 0;
    if (ResumeThread(process_info.hThread) == (DWORD)-1) {
        error_code = GetLastError();
        terminate_failed_launch(process_info.hProcess, process_info.hThread,
                                &primary_thread_suspended, &secondary_threads,
                                &secondary_thread_count);
        show_launch_error(L"resume-patched-game",
                          L"应用 Se 资源检查修复后无法恢复游戏。", error_code);
        execution->result = 14;
        goto cleanup_process;
    }
    primary_thread_suspended = FALSE;
    if (!set_game_display(process_info.hProcess, process_info.dwProcessId, settings)) {
        error_code = GetLastError();
        terminate_failed_launch(process_info.hProcess, process_info.hThread,
                                &primary_thread_suspended, &secondary_threads,
                                &secondary_thread_count);
        show_launch_error(L"set-display-mode",
                          L"游戏已启动，但无法应用所选显示模式。", error_code);
        execution->result = 15;
        goto cleanup_process;
    }

    write_log(L"game-started", ERROR_SUCCESS);
    execution->result = 0;
    execution->launched = TRUE;

cleanup_process:
    if (secondary_threads != NULL) {
        if (!resume_secondary_threads(secondary_threads, secondary_thread_count)) {
            write_log(L"resume-secondary-cleanup", GetLastError());
        }
    }
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
cleanup:
    release_launch_mutex(launch_mutex);
    close_log();
    return execution->launched;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show_command)
{
    wchar_t preflight_status[512] = L"";
    wchar_t settings_error[512] = L"";
    wchar_t diagnostics[2048];
    LauncherDialogContext dialog_context;
    LaunchExecutionContext execution;
    RequestedAction action;
    BOOL settings_loaded;
    BOOL force_settings;

    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show_command;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (!set_working_directory_to_launcher()) {
        show_error(L"无法切换到游戏目录。", GetLastError());
        return 1;
    }
    action = parse_requested_action();
    if (action == ACTION_INVALID) {
        show_error(L"启动参数无效。支持 --settings、--launch 和 --diagnose。",
                   ERROR_INVALID_PARAMETER);
        return 64;
    }

    ZeroMemory(&dialog_context, sizeof(dialog_context));
    ZeroMemory(&execution, sizeof(execution));
    dialog_context.start_callback = launch_game_with_settings;
    dialog_context.start_user_data = &execution;
    settings_loaded = launcher_settings_load(LAUNCHER_SETTINGS_FILE,
                                              DGVOODOO_CONFIG_FILE,
                                              &dialog_context.settings,
                                              &dialog_context.imported_legacy,
                                              settings_error,
                                              sizeof(settings_error) / sizeof(settings_error[0]));
    dialog_context.can_start = game_files_available(preflight_status,
                                                     sizeof(preflight_status) / sizeof(preflight_status[0]));
    if (!settings_loaded) {
        launcher_settings_recommended(&dialog_context.settings);
        dialog_context.status_message = settings_error;
    } else if (!dialog_context.can_start) {
        dialog_context.status_message = preflight_status;
    }
    if (action == ACTION_DIAGNOSE) {
        launcher_build_diagnostics(&dialog_context.settings, diagnostics,
                                   sizeof(diagnostics) / sizeof(diagnostics[0]));
        MessageBoxW(NULL, diagnostics, L"启动器诊断摘要",
                    MB_OK | (dialog_context.can_start ? MB_ICONINFORMATION : MB_ICONWARNING));
        return 0;
    }
    if (action == ACTION_LAUNCH && !settings_loaded) {
        open_log();
        write_log(L"settings-load", ERROR_INVALID_DATA);
        show_error(settings_error, ERROR_INVALID_DATA);
        close_log();
        return 65;
    }
    force_settings = action == ACTION_SETTINGS || !settings_loaded ||
                     !dialog_context.settings.quick_start ||
                     (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (action != ACTION_LAUNCH && force_settings) {
        if (launcher_show_dialog(instance, &dialog_context) != LAUNCHER_DIALOG_START) {
            return execution.result;
        }
        return execution.result;
    }
    launch_game_with_settings(&dialog_context.settings, &execution);
    return execution.result;
}
