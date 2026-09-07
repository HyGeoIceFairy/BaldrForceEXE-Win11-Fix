/*
 * BALDR FORCE EXE Win11 compatibility launcher
 * Copyright (c) 2026 BALDR FORCE EXE Win11 Fix contributors
 * SPDX-License-Identifier: MIT
 *
 * This launcher contains no game executable, translation, or game asset.
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <wchar.h>
#include <stdint.h>
#include <string.h>

#define GAME_IMAGE_SIGNATURE_ADDRESS ((LPCVOID)(uintptr_t)0x00461108)
#define FIND_FIRST_FILE_IAT_ADDRESS   ((LPCVOID)(uintptr_t)0x004BD0F0)
#define RESOURCE_NAME_ADDRESS         ((LPVOID)(uintptr_t)0x004DD190)
#define PATCH_TIMEOUT_MS              10000
#define ALIAS_MARKER                  L"Sx\\.baldrforce-se-alias"

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

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show_command)
{
    static const BYTE resource_alias[] = { 'S', 'x' };
    STARTUPINFOW startup;
    PROCESS_INFORMATION process_info;
    wchar_t game_command[] = L"\"BaldrForce.exe\"";
    SIZE_T written = 0;
    DWORD error_code;

    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show_command;

    if (!set_working_directory_to_launcher()) {
        show_error(L"无法切换到游戏目录。", GetLastError());
        return 1;
    }

    if (GetFileAttributesW(L"BaldrForce.exe") == INVALID_FILE_ATTRIBUTES) {
        show_error(L"启动器必须与 BaldrForce.exe 放在同一目录。", ERROR_FILE_NOT_FOUND);
        return 2;
    }
    if (GetFileAttributesW(L"Se\\Abort.wav") == INVALID_FILE_ATTRIBUTES) {
        show_error(L"音效资源目录 Se 不存在或不完整。",
                   ERROR_PATH_NOT_FOUND);
        return 3;
    }
    if (!ensure_sound_alias()) {
        show_error(L"无法建立完整的 Sx 音效资源别名目录。", GetLastError());
        return 4;
    }

    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process_info, sizeof(process_info));
    startup.cb = sizeof(startup);

    if (!CreateProcessW(NULL, game_command, NULL, NULL, FALSE, CREATE_SUSPENDED,
                        NULL, NULL, &startup, &process_info)) {
        show_error(L"无法启动 BaldrForce.exe。", GetLastError());
        return 5;
    }

    if (ResumeThread(process_info.hThread) == (DWORD)-1) {
        error_code = GetLastError();
        TerminateProcess(process_info.hProcess, 1);
        show_error(L"无法恢复游戏启动线程。", error_code);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return 6;
    }

    if (!wait_for_unpacked_image(process_info.hProcess)) {
        error_code = GetLastError();
        TerminateProcess(process_info.hProcess, 1);
        show_error(L"等待汉化封装器解包超时，未应用资源路径修复。", error_code);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return 7;
    }

    if (SuspendThread(process_info.hThread) == (DWORD)-1) {
        error_code = GetLastError();
        TerminateProcess(process_info.hProcess, 1);
        show_error(L"无法暂停游戏以应用 Se 到 Sx 的资源路径修复。", error_code);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return 8;
    }

    if (!WriteProcessMemory(process_info.hProcess, RESOURCE_NAME_ADDRESS,
                            resource_alias, sizeof(resource_alias), &written) ||
        written != sizeof(resource_alias) ||
        !FlushInstructionCache(process_info.hProcess, RESOURCE_NAME_ADDRESS,
                               sizeof(resource_alias))) {
        error_code = GetLastError();
        TerminateProcess(process_info.hProcess, 1);
        show_error(L"无法应用 Se 到 Sx 的资源路径修复。", error_code);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return 9;
    }

    if (ResumeThread(process_info.hThread) == (DWORD)-1) {
        error_code = GetLastError();
        TerminateProcess(process_info.hProcess, 1);
        show_error(L"应用 Se 资源检查修复后无法恢复游戏。", error_code);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return 10;
    }

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return 0;
}
