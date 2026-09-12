#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "launcher_config.h"

#define CONFIG_MAXIMUM_SIZE (2U * 1024U * 1024U)
#define TEMPORARY_PATH_CAPACITY 32768

typedef struct ManagedValue {
    const char *section;
    const char *key;
    const char *value;
    unsigned int matches;
} ManagedValue;

typedef struct ByteBuffer {
    char *data;
    size_t size;
    size_t capacity;
} ByteBuffer;

static void set_error(wchar_t *buffer, size_t count, const wchar_t *message)
{
    if (buffer == NULL || count == 0) {
        return;
    }
    wcsncpy(buffer, message, count - 1);
    buffer[count - 1] = L'\0';
}

static BOOL equals_ascii_ci(const char *left, size_t left_size, const char *right)
{
    size_t index;
    size_t right_size = strlen(right);

    if (left_size != right_size) {
        return FALSE;
    }
    for (index = 0; index < left_size; ++index) {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return FALSE;
        }
    }
    return TRUE;
}

static void trim_ascii(const char **start, const char **end)
{
    while (*start < *end && (**start == ' ' || **start == '\t')) {
        ++*start;
    }
    while (*end > *start && ((*end)[-1] == ' ' || (*end)[-1] == '\t')) {
        --*end;
    }
}

static BOOL buffer_reserve(ByteBuffer *buffer, size_t additional)
{
    size_t required;
    size_t capacity;
    char *replacement;

    if (additional > SIZE_MAX - buffer->size) {
        return FALSE;
    }
    required = buffer->size + additional;
    if (required <= buffer->capacity) {
        return TRUE;
    }
    capacity = buffer->capacity == 0 ? 4096 : buffer->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    replacement = (char *)realloc(buffer->data, capacity);
    if (replacement == NULL) {
        return FALSE;
    }
    buffer->data = replacement;
    buffer->capacity = capacity;
    return TRUE;
}

static BOOL buffer_append(ByteBuffer *buffer, const char *data, size_t size)
{
    if (!buffer_reserve(buffer, size)) {
        return FALSE;
    }
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return TRUE;
}

static BOOL read_file_bytes(const wchar_t *path, char **contents, size_t *size,
                            wchar_t *error_message, size_t error_message_count)
{
    HANDLE file;
    LARGE_INTEGER length;
    char *data;
    DWORD read_size;

    *contents = NULL;
    *size = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        set_error(error_message, error_message_count, L"无法读取配置文件。");
        return FALSE;
    }
    if (!GetFileSizeEx(file, &length) || length.QuadPart <= 0 ||
        length.QuadPart > CONFIG_MAXIMUM_SIZE) {
        CloseHandle(file);
        SetLastError(ERROR_FILE_TOO_LARGE);
        set_error(error_message, error_message_count, L"配置文件为空或大小异常。");
        return FALSE;
    }
    data = (char *)malloc((size_t)length.QuadPart + 1);
    if (data == NULL) {
        CloseHandle(file);
        SetLastError(ERROR_OUTOFMEMORY);
        set_error(error_message, error_message_count, L"读取配置文件时内存不足。");
        return FALSE;
    }
    if (!ReadFile(file, data, (DWORD)length.QuadPart, &read_size, NULL) ||
        read_size != (DWORD)length.QuadPart) {
        DWORD error_code = GetLastError();
        free(data);
        CloseHandle(file);
        SetLastError(error_code);
        set_error(error_message, error_message_count, L"无法完整读取配置文件。");
        return FALSE;
    }
    CloseHandle(file);
    data[read_size] = '\0';
    *contents = data;
    *size = read_size;
    return TRUE;
}

static BOOL atomic_write_bytes(const wchar_t *path, const char *contents, size_t size,
                               wchar_t *error_message, size_t error_message_count)
{
    wchar_t temporary_path[TEMPORARY_PATH_CAPACITY];
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD written;
    size_t total = 0;
    DWORD error_code = ERROR_SUCCESS;

    if (_snwprintf(temporary_path, TEMPORARY_PATH_CAPACITY - 1, L"%ls.tmp.%lu.%lu",
                   path, (unsigned long)GetCurrentProcessId(),
                   (unsigned long)GetTickCount()) < 0) {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        set_error(error_message, error_message_count, L"配置文件路径过长。");
        return FALSE;
    }
    temporary_path[TEMPORARY_PATH_CAPACITY - 1] = L'\0';
    file = CreateFileW(temporary_path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        set_error(error_message, error_message_count, L"无法创建临时配置文件。");
        return FALSE;
    }
    while (total < size) {
        DWORD chunk = (DWORD)((size - total) > 0x7fffffffU ? 0x7fffffffU : (size - total));
        if (!WriteFile(file, contents + total, chunk, &written, NULL) || written == 0) {
            error_code = GetLastError();
            break;
        }
        total += written;
    }
    if (error_code == ERROR_SUCCESS && !FlushFileBuffers(file)) {
        error_code = GetLastError();
    }
    if (!CloseHandle(file) && error_code == ERROR_SUCCESS) {
        error_code = GetLastError();
    }
    file = INVALID_HANDLE_VALUE;
    if (error_code == ERROR_SUCCESS &&
        !MoveFileExW(temporary_path, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error_code = GetLastError();
    }
    if (error_code != ERROR_SUCCESS) {
        DeleteFileW(temporary_path);
        SetLastError(error_code);
        set_error(error_message, error_message_count, L"无法安全替换配置文件。");
        return FALSE;
    }
    return TRUE;
}

void launcher_settings_recommended(LauncherSettings *settings)
{
    ZeroMemory(settings, sizeof(*settings));
    settings->display_mode = DISPLAY_MODE_BORDERLESS;
    settings->width = 960;
    settings->height = 720;
    settings->keep_aspect_ratio = TRUE;
    settings->smooth_scaling = TRUE;
    settings->capture_mouse = TRUE;
    settings->vertical_sync = TRUE;
    settings->center_window = TRUE;
    settings->quick_start = FALSE;
}

RequestedAction launcher_requested_action_parse(int argument_count,
                                                const wchar_t *argument)
{
    if (argument_count == 1) {
        return ACTION_AUTOMATIC;
    }
    if (argument_count != 2 || argument == NULL) {
        return ACTION_INVALID;
    }
    if (_wcsicmp(argument, L"--settings") == 0) {
        return ACTION_SETTINGS;
    }
    if (_wcsicmp(argument, L"--launch") == 0) {
        return ACTION_LAUNCH;
    }
    if (_wcsicmp(argument, L"--diagnose") == 0) {
        return ACTION_DIAGNOSE;
    }
    return ACTION_INVALID;
}

BOOL launcher_settings_validate(const LauncherSettings *settings,
                                unsigned int maximum_width,
                                unsigned int maximum_height,
                                wchar_t *error_message,
                                size_t error_message_count)
{
    if (settings->display_mode != DISPLAY_MODE_WINDOWED &&
        settings->display_mode != DISPLAY_MODE_BORDERLESS &&
        settings->display_mode != DISPLAY_MODE_EXCLUSIVE) {
        SetLastError(ERROR_INVALID_DATA);
        set_error(error_message, error_message_count, L"显示模式无效。");
        return FALSE;
    }
    if (settings->width < 320 || settings->height < 240 ||
        settings->width > 7680 || settings->height > 4320) {
        SetLastError(ERROR_INVALID_DATA);
        set_error(error_message, error_message_count,
                  L"窗口尺寸必须在 320×240 到 7680×4320 之间。");
        return FALSE;
    }
    if (settings->display_mode == DISPLAY_MODE_WINDOWED &&
        maximum_width != 0 && maximum_height != 0 &&
        (settings->width > maximum_width || settings->height > maximum_height)) {
        SetLastError(ERROR_INVALID_DATA);
        if (error_message != NULL && error_message_count != 0) {
            _snwprintf(error_message, error_message_count - 1,
                       L"窗口尺寸超过当前显示器的可用区域；最大客户区约为 %u×%u。",
                       maximum_width, maximum_height);
            error_message[error_message_count - 1] = L'\0';
        }
        return FALSE;
    }
    return TRUE;
}

static BOOL parse_bool_string(const wchar_t *text, BOOL *value)
{
    if (_wcsicmp(text, L"true") == 0 || wcscmp(text, L"1") == 0) {
        *value = TRUE;
        return TRUE;
    }
    if (_wcsicmp(text, L"false") == 0 || wcscmp(text, L"0") == 0) {
        *value = FALSE;
        return TRUE;
    }
    return FALSE;
}

static BOOL read_ini_bool(const wchar_t *path, const wchar_t *key, BOOL *value)
{
    wchar_t text[32];
    GetPrivateProfileStringW(L"Launcher", key, L"", text,
                             (DWORD)(sizeof(text) / sizeof(text[0])), path);
    return parse_bool_string(text, value);
}

static BOOL resolve_full_path(const wchar_t *path, wchar_t *resolved,
                              DWORD resolved_count, wchar_t *error_message,
                              size_t error_message_count)
{
    DWORD length = GetFullPathNameW(path, resolved_count, resolved, NULL);
    if (length == 0 || length >= resolved_count) {
        if (length >= resolved_count) {
            SetLastError(ERROR_BUFFER_OVERFLOW);
        }
        set_error(error_message, error_message_count, L"设置文件路径过长或无效。");
        return FALSE;
    }
    return TRUE;
}

static BOOL find_dg_value(const char *input, size_t input_size,
                          const char *wanted_section, const char *wanted_key,
                          char *value, size_t value_capacity)
{
    const char *cursor = input;
    const char *input_end = input + input_size;
    char section[64] = "";

    while (cursor < input_end) {
        const char *line_start = cursor;
        const char *line_end;
        const char *trimmed_start;
        const char *trimmed_end;
        const char *equals;

        while (cursor < input_end && *cursor != '\r' && *cursor != '\n') {
            ++cursor;
        }
        line_end = cursor;
        if (cursor < input_end && *cursor == '\r') {
            ++cursor;
        }
        if (cursor < input_end && *cursor == '\n') {
            ++cursor;
        }
        trimmed_start = line_start;
        trimmed_end = line_end;
        trim_ascii(&trimmed_start, &trimmed_end);
        if (line_start == input && trimmed_end - trimmed_start >= 3 &&
            (unsigned char)trimmed_start[0] == 0xef &&
            (unsigned char)trimmed_start[1] == 0xbb &&
            (unsigned char)trimmed_start[2] == 0xbf) {
            trimmed_start += 3;
        }
        if (trimmed_end > trimmed_start + 2 && *trimmed_start == '[' &&
            trimmed_end[-1] == ']') {
            size_t section_size = (size_t)(trimmed_end - trimmed_start - 2);
            if (section_size >= sizeof(section)) {
                section_size = sizeof(section) - 1;
            }
            memcpy(section, trimmed_start + 1, section_size);
            section[section_size] = '\0';
            continue;
        }
        equals = (const char *)memchr(trimmed_start, '=', (size_t)(trimmed_end - trimmed_start));
        if (equals != NULL && _stricmp(section, wanted_section) == 0) {
            const char *key_start = trimmed_start;
            const char *key_end = equals;
            const char *value_start = equals + 1;
            const char *value_end = trimmed_end;
            const char *comment;
            size_t value_size;
            trim_ascii(&key_start, &key_end);
            if (!equals_ascii_ci(key_start, (size_t)(key_end - key_start), wanted_key)) {
                continue;
            }
            comment = (const char *)memchr(value_start, ';', (size_t)(value_end - value_start));
            if (comment != NULL) {
                value_end = comment;
            }
            trim_ascii(&value_start, &value_end);
            value_size = (size_t)(value_end - value_start);
            if (value_size >= value_capacity) {
                return FALSE;
            }
            memcpy(value, value_start, value_size);
            value[value_size] = '\0';
            return TRUE;
        }
    }
    return FALSE;
}

static void import_dgvoodoo_defaults(const wchar_t *path, LauncherSettings *settings)
{
    char *contents;
    size_t size;
    char value[64];
    wchar_t unused[2];

    if (!read_file_bytes(path, &contents, &size, unused, 2)) {
        return;
    }
    if (find_dg_value(contents, size, "General", "FullScreenMode", value, sizeof(value))) {
        settings->display_mode = _stricmp(value, "true") == 0
                                     ? DISPLAY_MODE_BORDERLESS
                                     : DISPLAY_MODE_WINDOWED;
    }
    if (find_dg_value(contents, size, "General", "ScalingMode", value, sizeof(value))) {
        settings->keep_aspect_ratio = strstr(value, "_ar") != NULL;
    }
    if (find_dg_value(contents, size, "General", "CaptureMouse", value, sizeof(value))) {
        settings->capture_mouse = _stricmp(value, "true") == 0;
    }
    if (find_dg_value(contents, size, "General", "CenterAppWindow", value, sizeof(value))) {
        settings->center_window = _stricmp(value, "true") == 0;
    }
    if (find_dg_value(contents, size, "GeneralExt", "Resampling", value, sizeof(value))) {
        settings->smooth_scaling = _stricmp(value, "pointsampled") != 0;
    }
    if (find_dg_value(contents, size, "DirectX", "ForceVerticalSync", value, sizeof(value))) {
        settings->vertical_sync = _stricmp(value, "true") == 0;
    }
    free(contents);
}

BOOL launcher_settings_load(const wchar_t *settings_path,
                            const wchar_t *dgvoodoo_path,
                            LauncherSettings *settings,
                            BOOL *imported_legacy,
                            wchar_t *error_message,
                            size_t error_message_count)
{
    wchar_t absolute_settings_path[TEMPORARY_PATH_CAPACITY];
    wchar_t text[64];
    int schema;

    launcher_settings_recommended(settings);
    *imported_legacy = FALSE;
    if (GetFileAttributesW(settings_path) == INVALID_FILE_ATTRIBUTES) {
        DWORD error_code = GetLastError();
        if (error_code == ERROR_FILE_NOT_FOUND || error_code == ERROR_PATH_NOT_FOUND) {
            import_dgvoodoo_defaults(dgvoodoo_path, settings);
            *imported_legacy = TRUE;
            return TRUE;
        }
        SetLastError(error_code);
        set_error(error_message, error_message_count, L"无法检查启动器设置文件。");
        return FALSE;
    }
    if (!resolve_full_path(settings_path, absolute_settings_path,
                           TEMPORARY_PATH_CAPACITY,
                           error_message, error_message_count)) {
        return FALSE;
    }
    schema = GetPrivateProfileIntW(L"Launcher", L"SchemaVersion", 0,
                                   absolute_settings_path);
    if (schema != 1) {
        SetLastError(ERROR_INVALID_DATA);
        set_error(error_message, error_message_count,
                  L"启动器设置文件版本无效。可将其移走后重新打开启动器。");
        return FALSE;
    }
    GetPrivateProfileStringW(L"Launcher", L"DisplayMode", L"", text,
                             (DWORD)(sizeof(text) / sizeof(text[0])),
                             absolute_settings_path);
    if (_wcsicmp(text, L"windowed") == 0) {
        settings->display_mode = DISPLAY_MODE_WINDOWED;
    } else if (_wcsicmp(text, L"borderless") == 0) {
        settings->display_mode = DISPLAY_MODE_BORDERLESS;
    } else if (_wcsicmp(text, L"exclusive") == 0 ||
               _wcsicmp(text, L"fullscreen") == 0) {
        settings->display_mode = DISPLAY_MODE_EXCLUSIVE;
    } else {
        SetLastError(ERROR_INVALID_DATA);
        set_error(error_message, error_message_count, L"设置文件中的显示模式无效。");
        return FALSE;
    }
    settings->width = (unsigned int)GetPrivateProfileIntW(L"Launcher", L"Width", 0,
                                                          absolute_settings_path);
    settings->height = (unsigned int)GetPrivateProfileIntW(L"Launcher", L"Height", 0,
                                                           absolute_settings_path);
    if (!read_ini_bool(absolute_settings_path, L"KeepAspectRatio", &settings->keep_aspect_ratio) ||
        !read_ini_bool(absolute_settings_path, L"SmoothScaling", &settings->smooth_scaling) ||
        !read_ini_bool(absolute_settings_path, L"CaptureMouse", &settings->capture_mouse) ||
        !read_ini_bool(absolute_settings_path, L"VerticalSync", &settings->vertical_sync) ||
        !read_ini_bool(absolute_settings_path, L"CenterWindow", &settings->center_window) ||
        !read_ini_bool(absolute_settings_path, L"QuickStart", &settings->quick_start)) {
        SetLastError(ERROR_INVALID_DATA);
        set_error(error_message, error_message_count, L"启动器设置文件不完整或包含无效值。");
        return FALSE;
    }
    return launcher_settings_validate(settings, 0, 0, error_message, error_message_count);
}

BOOL launcher_settings_save(const wchar_t *settings_path,
                            const LauncherSettings *settings,
                            wchar_t *error_message,
                            size_t error_message_count)
{
    char contents[768];
    int length;

    if (!launcher_settings_validate(settings, 0, 0, error_message, error_message_count)) {
        return FALSE;
    }
    length = snprintf(contents, sizeof(contents),
                      "; BALDR FORCE EXE Win11 Fix launcher settings\r\n"
                      "; Hold Shift while starting to reopen the settings window.\r\n"
                      "[Launcher]\r\n"
                      "SchemaVersion=1\r\n"
                      "DisplayMode=%s\r\n"
                      "Width=%u\r\nHeight=%u\r\n"
                      "KeepAspectRatio=%s\r\nSmoothScaling=%s\r\n"
                      "CaptureMouse=%s\r\nVerticalSync=%s\r\n"
                      "CenterWindow=%s\r\nQuickStart=%s\r\n",
                      settings->display_mode == DISPLAY_MODE_WINDOWED
                          ? "windowed"
                          : (settings->display_mode == DISPLAY_MODE_BORDERLESS
                                 ? "borderless" : "exclusive"),
                      settings->width, settings->height,
                      settings->keep_aspect_ratio ? "true" : "false",
                      settings->smooth_scaling ? "true" : "false",
                      settings->capture_mouse ? "true" : "false",
                      settings->vertical_sync ? "true" : "false",
                      settings->center_window ? "true" : "false",
                      settings->quick_start ? "true" : "false");
    if (length < 0 || (size_t)length >= sizeof(contents)) {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        set_error(error_message, error_message_count, L"无法生成启动器设置。");
        return FALSE;
    }
    return atomic_write_bytes(settings_path, contents, (size_t)length,
                              error_message, error_message_count);
}

BOOL dgvoodoo_config_transform(const char *input,
                              size_t input_size,
                              const LauncherSettings *settings,
                              char **output,
                              size_t *output_size,
                              wchar_t *error_message,
                              size_t error_message_count)
{
    ManagedValue values[] = {
        { "General", "FullScreenMode",
          settings->display_mode == DISPLAY_MODE_EXCLUSIVE ? "true" : "false", 0 },
        { "General", "ScalingMode", settings->keep_aspect_ratio ? "stretched_ar" : "stretched", 0 },
        { "General", "KeepWindowAspectRatio", settings->keep_aspect_ratio ? "true" : "false", 0 },
        { "General", "CaptureMouse", settings->capture_mouse ? "true" : "false", 0 },
        { "General", "CenterAppWindow", "false", 0 },
        { "GeneralExt", "Resampling", settings->smooth_scaling ? "bilinear" : "pointsampled", 0 },
        { "GeneralExt", "WindowedAttributes", "", 0 },
        { "GeneralExt", "FullscreenAttributes", "", 0 },
        { "DirectX", "AppControlledScreenMode",
          settings->display_mode == DISPLAY_MODE_EXCLUSIVE ? "true" : "false", 0 },
        { "DirectX", "DisableAltEnterToToggleScreenMode", "true", 0 },
        { "DirectX", "ForceVerticalSync", settings->vertical_sync ? "true" : "false", 0 }
    };
    const size_t value_count = sizeof(values) / sizeof(values[0]);
    const char *cursor = input;
    const char *input_end = input + input_size;
    char section[64] = "";
    ByteBuffer result = { 0 };
    size_t index;

    *output = NULL;
    *output_size = 0;
    if (input_size >= 2 &&
        (((unsigned char)input[0] == 0xff && (unsigned char)input[1] == 0xfe) ||
         ((unsigned char)input[0] == 0xfe && (unsigned char)input[1] == 0xff))) {
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        set_error(error_message, error_message_count,
                  L"不支持 UTF-16 编码的 dgVoodoo.conf；请另存为 ANSI 或 UTF-8。");
        return FALSE;
    }
    while (cursor < input_end) {
        const char *line_start = cursor;
        const char *line_end;
        const char *next_line;
        const char *trimmed_start;
        const char *trimmed_end;
        const char *equals;
        ManagedValue *managed = NULL;

        while (cursor < input_end && *cursor != '\r' && *cursor != '\n') {
            ++cursor;
        }
        line_end = cursor;
        if (cursor < input_end && *cursor == '\r') {
            ++cursor;
        }
        if (cursor < input_end && *cursor == '\n') {
            ++cursor;
        }
        next_line = cursor;
        trimmed_start = line_start;
        trimmed_end = line_end;
        trim_ascii(&trimmed_start, &trimmed_end);
        if (line_start == input && trimmed_end - trimmed_start >= 3 &&
            (unsigned char)trimmed_start[0] == 0xef &&
            (unsigned char)trimmed_start[1] == 0xbb &&
            (unsigned char)trimmed_start[2] == 0xbf) {
            trimmed_start += 3;
        }
        if (trimmed_end > trimmed_start + 2 && *trimmed_start == '[' &&
            trimmed_end[-1] == ']') {
            size_t section_size = (size_t)(trimmed_end - trimmed_start - 2);
            if (section_size >= sizeof(section)) {
                section_size = sizeof(section) - 1;
            }
            memcpy(section, trimmed_start + 1, section_size);
            section[section_size] = '\0';
        } else {
            equals = (const char *)memchr(trimmed_start, '=',
                                          (size_t)(trimmed_end - trimmed_start));
            if (equals != NULL) {
                const char *key_start = trimmed_start;
                const char *key_end = equals;
                trim_ascii(&key_start, &key_end);
                for (index = 0; index < value_count; ++index) {
                    if (_stricmp(section, values[index].section) == 0 &&
                        equals_ascii_ci(key_start, (size_t)(key_end - key_start),
                                        values[index].key)) {
                        managed = &values[index];
                        break;
                    }
                }
            }
        }
        if (managed == NULL) {
            if (!buffer_append(&result, line_start, (size_t)(next_line - line_start))) {
                goto out_of_memory;
            }
        } else {
            const char *value_start = equals + 1;
            const char *comment;
            managed->matches++;
            while (value_start < line_end && (*value_start == ' ' || *value_start == '\t')) {
                ++value_start;
            }
            comment = (const char *)memchr(value_start, ';', (size_t)(line_end - value_start));
            if (!buffer_append(&result, line_start, (size_t)(value_start - line_start)) ||
                !buffer_append(&result, managed->value, strlen(managed->value))) {
                goto out_of_memory;
            }
            if (comment != NULL) {
                const char *suffix = comment;
                while (suffix > value_start && (suffix[-1] == ' ' || suffix[-1] == '\t')) {
                    --suffix;
                }
                if (!buffer_append(&result, suffix, (size_t)(line_end - suffix))) {
                    goto out_of_memory;
                }
            }
            if (!buffer_append(&result, line_end, (size_t)(next_line - line_end))) {
                goto out_of_memory;
            }
        }
    }
    for (index = 0; index < value_count; ++index) {
        if (values[index].matches != 1) {
            free(result.data);
            SetLastError(ERROR_INVALID_DATA);
            set_error(error_message, error_message_count,
                      L"dgVoodoo.conf 缺少必要选项或包含重复选项，未修改文件。");
            return FALSE;
        }
    }
    if (!buffer_reserve(&result, 1)) {
        goto out_of_memory;
    }
    result.data[result.size] = '\0';
    *output = result.data;
    *output_size = result.size;
    return TRUE;

out_of_memory:
    free(result.data);
    SetLastError(ERROR_OUTOFMEMORY);
    set_error(error_message, error_message_count, L"生成兼容层配置时内存不足。");
    return FALSE;
}

BOOL dgvoodoo_config_apply(const wchar_t *config_path,
                          const wchar_t *backup_path,
                          const LauncherSettings *settings,
                          wchar_t *error_message,
                          size_t error_message_count)
{
    char *input;
    char *output;
    size_t input_size;
    size_t output_size;

    if (!read_file_bytes(config_path, &input, &input_size,
                         error_message, error_message_count)) {
        return FALSE;
    }
    if (!dgvoodoo_config_transform(input, input_size, settings, &output, &output_size,
                                   error_message, error_message_count)) {
        free(input);
        return FALSE;
    }
    if (output_size == input_size && memcmp(output, input, input_size) == 0) {
        free(output);
        free(input);
        return TRUE;
    }
    {
        DWORD backup_attributes = GetFileAttributesW(backup_path);
        if (backup_attributes == INVALID_FILE_ATTRIBUTES) {
            DWORD backup_error = GetLastError();
            if (backup_error != ERROR_FILE_NOT_FOUND &&
                backup_error != ERROR_PATH_NOT_FOUND) {
                free(output);
                free(input);
                SetLastError(backup_error);
                set_error(error_message, error_message_count,
                          L"无法检查 dgVoodoo.conf 的首次修改备份。");
                return FALSE;
            }
            if (!CopyFileW(config_path, backup_path, TRUE)) {
                DWORD error_code = GetLastError();
                free(output);
                free(input);
                SetLastError(error_code);
                set_error(error_message, error_message_count,
                          L"无法创建 dgVoodoo.conf 的首次修改备份。");
                return FALSE;
            }
        } else if ((backup_attributes & FILE_ATTRIBUTE_DIRECTORY) ||
                   (backup_attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            free(output);
            free(input);
            SetLastError(ERROR_INVALID_DATA);
            set_error(error_message, error_message_count,
                      L"dgVoodoo.conf 备份路径不是普通文件，未修改配置。");
            return FALSE;
        }
    }
    free(input);
    if (!atomic_write_bytes(config_path, output, output_size,
                            error_message, error_message_count)) {
        free(output);
        return FALSE;
    }
    free(output);
    return TRUE;
}
