#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "../src/launcher_config.h"

static int failures = 0;

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s (line %d, error %lu)\n", \
                    message, __LINE__, (unsigned long)GetLastError()); \
            ++failures; \
        } \
    } while (0)

static const char valid_config[] =
    "; preserved header\r\n"
    "[General]\r\n"
    "FullScreenMode = true ; old value\r\n"
    "ScalingMode = stretched_ar\r\n"
    "KeepWindowAspectRatio = true\r\n"
    "CaptureMouse = true\r\n"
    "CenterAppWindow = false\r\n"
    "UnknownGeneral = keep-me\r\n"
    "[GeneralExt]\r\n"
    "Resampling = bilinear\r\n"
    "FreeMouse = false\r\n"
    "WindowedAttributes = \r\n"
    "FullscreenAttributes = \r\n"
    "[Glide]\r\n"
    "Resolution = unforced\r\n"
    "[DirectX]\r\n"
    "Resolution = unforced\r\n"
    "AppControlledScreenMode = true\r\n"
    "DisableAltEnterToToggleScreenMode = true\r\n"
    "ForceVerticalSync = false\r\n"
    "[Tail]\r\n"
    "UserValue = untouched\r\n";

static BOOL write_bytes(const wchar_t *path, const char *data, size_t size)
{
    HANDLE file;
    DWORD written = 0;
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    if (!WriteFile(file, data, (DWORD)size, &written, NULL) || written != size) {
        DWORD error_code = GetLastError();
        CloseHandle(file);
        SetLastError(error_code);
        return FALSE;
    }
    return CloseHandle(file);
}

static char *read_bytes(const wchar_t *path, size_t *size)
{
    HANDLE file;
    LARGE_INTEGER length;
    DWORD read_size;
    char *data;

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &length)) {
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        return NULL;
    }
    data = (char *)malloc((size_t)length.QuadPart + 1);
    if (data == NULL) {
        CloseHandle(file);
        return NULL;
    }
    if (!ReadFile(file, data, (DWORD)length.QuadPart, &read_size, NULL)) {
        free(data);
        CloseHandle(file);
        return NULL;
    }
    CloseHandle(file);
    data[read_size] = '\0';
    *size = read_size;
    return data;
}

static void test_transform_preserves_unknown_content(void)
{
    LauncherSettings settings;
    char *output = NULL;
    size_t output_size = 0;
    wchar_t error_message[256] = L"";

    launcher_settings_recommended(&settings);
    settings.display_mode = DISPLAY_MODE_WINDOWED;
    settings.keep_aspect_ratio = FALSE;
    settings.smooth_scaling = FALSE;
    settings.capture_mouse = FALSE;
    settings.vertical_sync = TRUE;

    CHECK(dgvoodoo_config_transform(valid_config, sizeof(valid_config) - 1,
                                    &settings, &output, &output_size,
                                    error_message, 256),
          "valid dgVoodoo configuration transforms");
    if (output != NULL) {
        CHECK(strstr(output, "FullScreenMode = false ; old value") != NULL,
              "managed value changes and inline comment survives");
        CHECK(strstr(output, "ScalingMode = stretched\r\n") != NULL,
              "stretch mode changes without touching another section");
        CHECK(strstr(output, "Resampling = pointsampled") != NULL,
              "point sampling is written");
        CHECK(strstr(output, "FreeMouse = true") != NULL,
              "windowed mode exposes physical mouse coordinates for client conversion");
        CHECK(strstr(output, "AppControlledScreenMode = false") != NULL,
              "windowed mode lets dgVoodoo host the native DirectDraw lifecycle");
        CHECK(strstr(output, "ForceVerticalSync = true") != NULL,
              "DirectX vertical sync is written");
        CHECK(strstr(output, "UnknownGeneral = keep-me") != NULL,
              "unknown General key survives");
        CHECK(strstr(output, "[Glide]\r\nResolution = unforced") != NULL,
              "same-name key in Glide section survives");
        CHECK(strstr(output, "[Tail]\r\nUserValue = untouched") != NULL,
              "unknown section survives");
        CHECK(output_size > 0, "output size is populated");
        free(output);
    }
}

static void test_transform_couples_mouse_mode_and_client_conversion(void)
{
    LauncherSettings settings;
    char *output = NULL;
    size_t output_size = 0;
    wchar_t error_message[256] = L"";

    launcher_settings_recommended(&settings);
    settings.display_mode = DISPLAY_MODE_WINDOWED;
    settings.capture_mouse = TRUE;
    CHECK(dgvoodoo_config_transform(valid_config, sizeof(valid_config) - 1,
                                    &settings, &output, &output_size,
                                    error_message, 256),
          "captured windowed mouse configuration transforms");
    if (output != NULL) {
        CHECK(strstr(output, "CaptureMouse = true") != NULL &&
              strstr(output, "FreeMouse = true") != NULL,
              "captured windowed mouse exposes physical coordinates");
        free(output);
        output = NULL;
    }

    settings.capture_mouse = FALSE;
    CHECK(dgvoodoo_config_transform(valid_config, sizeof(valid_config) - 1,
                                    &settings, &output, &output_size,
                                    error_message, 256),
          "free windowed mouse configuration transforms");
    if (output != NULL) {
        CHECK(strstr(output, "CaptureMouse = false") != NULL &&
              strstr(output, "FreeMouse = true") != NULL,
              "uncaptured windowed mouse exposes physical coordinates");
        free(output);
        output = NULL;
    }

    settings.display_mode = DISPLAY_MODE_BORDERLESS;
    CHECK(dgvoodoo_config_transform(valid_config, sizeof(valid_config) - 1,
                                    &settings, &output, &output_size,
                                    error_message, 256),
          "borderless mouse configuration transforms");
    if (output != NULL) {
        CHECK(strstr(output, "FreeMouse = false") != NULL &&
              strstr(output, "AppControlledScreenMode = false") != NULL,
              "borderless mode retains dgVoodoo display and mouse mapping");
        free(output);
        output = NULL;
    }

    settings.display_mode = DISPLAY_MODE_EXCLUSIVE;
    CHECK(dgvoodoo_config_transform(valid_config, sizeof(valid_config) - 1,
                                    &settings, &output, &output_size,
                                    error_message, 256),
          "exclusive mouse configuration transforms");
    if (output != NULL) {
        CHECK(strstr(output, "FreeMouse = false") != NULL &&
              strstr(output, "AppControlledScreenMode = true") != NULL,
              "exclusive mode retains native display and dgVoodoo mouse mapping");
        free(output);
    }
}

static void test_transform_rejects_duplicate(void)
{
    LauncherSettings settings;
    char duplicate[sizeof(valid_config) + 64];
    char *output = NULL;
    size_t output_size = 0;
    wchar_t error_message[256] = L"";
    const char *needle = "FullScreenMode = true ; old value\r\n";
    const char *position = strstr(valid_config, needle);
    size_t prefix_size = (size_t)(position - valid_config) + strlen(needle);

    launcher_settings_recommended(&settings);
    memcpy(duplicate, valid_config, prefix_size);
    memcpy(duplicate + prefix_size, "FullScreenMode = false\r\n", 24);
    strcpy(duplicate + prefix_size + 24, valid_config + prefix_size);
    CHECK(!dgvoodoo_config_transform(duplicate, strlen(duplicate), &settings,
                                     &output, &output_size,
                                     error_message, 256),
          "duplicate managed key is rejected");
    CHECK(output == NULL, "rejected transform returns no partial output");
}

static void test_transform_accepts_utf8_bom(void)
{
    LauncherSettings settings;
    char with_bom[sizeof(valid_config) + 3];
    char *output = NULL;
    size_t output_size = 0;
    wchar_t error_message[256] = L"";

    with_bom[0] = (char)0xef;
    with_bom[1] = (char)0xbb;
    with_bom[2] = (char)0xbf;
    memcpy(with_bom + 3, valid_config, sizeof(valid_config));
    launcher_settings_recommended(&settings);
    CHECK(dgvoodoo_config_transform(with_bom, sizeof(valid_config) + 2,
                                    &settings, &output, &output_size,
                                    error_message, 256),
          "UTF-8 BOM configuration transforms");
    if (output != NULL) {
        CHECK(output_size >= 3 && (unsigned char)output[0] == 0xef &&
              (unsigned char)output[1] == 0xbb &&
              (unsigned char)output[2] == 0xbf,
              "UTF-8 BOM survives transformation");
        free(output);
    }
}

static void test_transform_preserves_lf_line_endings(void)
{
    LauncherSettings settings;
    char lf_config[sizeof(valid_config)];
    char *source = lf_config;
    const char *cursor = valid_config;
    char *output = NULL;
    size_t output_size = 0;
    wchar_t error_message[256] = L"";

    while (*cursor != '\0') {
        if (*cursor == '\r' && cursor[1] == '\n') {
            ++cursor;
        }
        *source++ = *cursor++;
    }
    launcher_settings_recommended(&settings);
    CHECK(dgvoodoo_config_transform(lf_config, (size_t)(source - lf_config),
                                    &settings, &output, &output_size,
                                    error_message, 256),
          "LF configuration transforms");
    if (output != NULL) {
        CHECK(memchr(output, '\r', output_size) == NULL,
              "LF line endings survive transformation");
        free(output);
    }
}

static void test_transform_rejects_missing_and_utf16(void)
{
    LauncherSettings settings;
    char missing[sizeof(valid_config)];
    const unsigned char utf16[] = { 0xff, 0xfe, '[', 0, 'G', 0 };
    char *output = NULL;
    size_t output_size = 0;
    wchar_t error_message[256] = L"";
    const char *line = "ForceVerticalSync = false\r\n";
    const char *position = strstr(valid_config, line);
    size_t prefix_size = (size_t)(position - valid_config);

    memcpy(missing, valid_config, prefix_size);
    strcpy(missing + prefix_size, position + strlen(line));
    launcher_settings_recommended(&settings);
    CHECK(!dgvoodoo_config_transform(missing, strlen(missing), &settings,
                                     &output, &output_size,
                                     error_message, 256),
          "missing managed key is rejected");
    CHECK(output == NULL, "missing-key rejection returns no output");
    CHECK(!dgvoodoo_config_transform((const char *)utf16, sizeof(utf16), &settings,
                                     &output, &output_size,
                                     error_message, 256),
          "UTF-16 configuration is rejected");
}

static void test_validation(void)
{
    LauncherSettings settings;
    wchar_t error_message[256] = L"";

    launcher_settings_recommended(&settings);
    settings.display_mode = DISPLAY_MODE_WINDOWED;
    settings.width = 960;
    settings.height = 720;
    CHECK(launcher_settings_validate(&settings, 1920, 1040,
                                     error_message, 256),
          "normal window size validates");
    settings.width = 0;
    CHECK(!launcher_settings_validate(&settings, 1920, 1040,
                                      error_message, 256),
          "zero width is rejected");
    settings.width = 2560;
    settings.height = 1440;
    CHECK(!launcher_settings_validate(&settings, 1920, 1040,
                                      error_message, 256),
          "window larger than work area is rejected");
    settings.display_mode = DISPLAY_MODE_BORDERLESS;
    CHECK(launcher_settings_validate(&settings, 1920, 1040,
                                     error_message, 256),
          "stored window size does not block fullscreen");
}

static void test_file_round_trip_and_backup(void)
{
    wchar_t test_root[MAX_PATH];
    wchar_t directory[MAX_PATH];
    wchar_t settings_path[MAX_PATH];
    wchar_t config_path[MAX_PATH];
    wchar_t backup_path[MAX_PATH];
    wchar_t original_directory[MAX_PATH];
    LauncherSettings expected;
    LauncherSettings loaded;
    BOOL imported = FALSE;
    wchar_t error_message[256] = L"";
    char *backup;
    size_t backup_size = 0;

    CHECK(GetFullPathNameW(L"build\\test-sandboxes", MAX_PATH, test_root, NULL) > 0,
          "workspace test root resolves");
    CreateDirectoryW(L"build", NULL);
    CreateDirectoryW(test_root, NULL);
    _snwprintf(directory, MAX_PATH - 1, L"%ls\\BaldrForceConfigTest-%lu-%lu",
               test_root, (unsigned long)GetCurrentProcessId(),
               (unsigned long)GetTickCount());
    directory[MAX_PATH - 1] = L'\0';
    CHECK(CreateDirectoryW(directory, NULL), "isolated test directory is created");
    _snwprintf(settings_path, MAX_PATH - 1, L"%ls\\settings.ini", directory);
    _snwprintf(config_path, MAX_PATH - 1, L"%ls\\dgVoodoo.conf", directory);
    _snwprintf(backup_path, MAX_PATH - 1, L"%ls\\dgVoodoo.backup", directory);

    CHECK(write_bytes(config_path, valid_config, sizeof(valid_config) - 1),
          "fixture configuration is written");
    launcher_settings_recommended(&loaded);
    CHECK(launcher_settings_load(settings_path, config_path, &loaded, &imported,
                                 error_message, 256),
          "missing launcher settings import the legacy dgVoodoo configuration");
    CHECK(imported && loaded.display_mode == DISPLAY_MODE_BORDERLESS,
          "legacy fullscreen intent migrates to recommended borderless mode");
    launcher_settings_recommended(&expected);
    expected.display_mode = DISPLAY_MODE_WINDOWED;
    expected.width = 1024;
    expected.height = 768;
    expected.quick_start = TRUE;
    expected.smooth_scaling = FALSE;
    CHECK(launcher_settings_save(settings_path, &expected, error_message, 256),
          "settings save atomically");
    CHECK(launcher_settings_load(settings_path, config_path, &loaded, &imported,
                                 error_message, 256),
          "saved settings load");
    CHECK(!imported, "existing settings are not reported as legacy import");
    CHECK(loaded.display_mode == expected.display_mode &&
          loaded.width == expected.width && loaded.height == expected.height &&
          loaded.quick_start == expected.quick_start &&
          loaded.smooth_scaling == expected.smooth_scaling,
          "settings round trip without value loss");
    CHECK(GetCurrentDirectoryW(MAX_PATH, original_directory) > 0,
          "original current directory is captured");
    CHECK(SetCurrentDirectoryW(directory), "test directory becomes current directory");
    CHECK(launcher_settings_load(L"settings.ini", L"dgVoodoo.conf", &loaded,
                                 &imported, error_message, 256),
          "relative settings path resolves against the current directory");
    CHECK(loaded.display_mode == expected.display_mode && loaded.quick_start,
          "relative settings load reads the expected file");
    CHECK(SetCurrentDirectoryW(original_directory),
          "original current directory is restored");
    CHECK(dgvoodoo_config_apply(config_path, backup_path, &expected,
                                error_message, 256),
          "configuration applies with first backup");
    backup = read_bytes(backup_path, &backup_size);
    CHECK(backup != NULL && backup_size == sizeof(valid_config) - 1 &&
          memcmp(backup, valid_config, sizeof(valid_config) - 1) == 0,
          "first backup contains original bytes");
    free(backup);
    expected.vertical_sync = FALSE;
    CHECK(dgvoodoo_config_apply(config_path, backup_path, &expected,
                                error_message, 256),
          "second configuration update succeeds");
    backup = read_bytes(backup_path, &backup_size);
    CHECK(backup != NULL && backup_size == sizeof(valid_config) - 1 &&
          memcmp(backup, valid_config, sizeof(valid_config) - 1) == 0,
          "later update does not overwrite first backup");
    free(backup);

    CHECK(DeleteFileW(settings_path), "test settings file is removed");
    CHECK(DeleteFileW(config_path), "test config file is removed");
    CHECK(DeleteFileW(backup_path), "test backup file is removed");
    CHECK(RemoveDirectoryW(directory), "empty isolated test directory is removed");
}

static void test_configuration_failure_paths(void)
{
    wchar_t test_root[MAX_PATH];
    wchar_t directory[MAX_PATH];
    wchar_t config_path[MAX_PATH];
    wchar_t backup_path[MAX_PATH];
    wchar_t blocked_path[MAX_PATH];
    LauncherSettings settings;
    wchar_t error_message[256] = L"";

    CHECK(GetFullPathNameW(L"build\\test-sandboxes", MAX_PATH, test_root, NULL) > 0,
          "workspace failure-test root resolves");
    CreateDirectoryW(L"build", NULL);
    CreateDirectoryW(test_root, NULL);
    _snwprintf(directory, MAX_PATH - 1, L"%ls\\BaldrForceFailureTest-%lu-%lu",
               test_root, (unsigned long)GetCurrentProcessId(),
               (unsigned long)GetTickCount());
    directory[MAX_PATH - 1] = L'\0';
    CHECK(CreateDirectoryW(directory, NULL), "failure-test directory is created");
    _snwprintf(config_path, MAX_PATH - 1, L"%ls\\dgVoodoo.conf", directory);
    _snwprintf(backup_path, MAX_PATH - 1, L"%ls\\dgVoodoo.backup", directory);
    _snwprintf(blocked_path, MAX_PATH - 1, L"%ls\\blocked", directory);
    CHECK(write_bytes(config_path, valid_config, sizeof(valid_config) - 1),
          "failure-test fixture is written");
    launcher_settings_recommended(&settings);
    settings.display_mode = DISPLAY_MODE_WINDOWED;

    CHECK(SetFileAttributesW(config_path, FILE_ATTRIBUTE_READONLY),
          "configuration becomes read-only");
    CHECK(!dgvoodoo_config_apply(config_path, backup_path, &settings,
                                 error_message, 256),
          "read-only configuration replacement fails safely");
    CHECK(GetFileAttributesW(backup_path) != INVALID_FILE_ATTRIBUTES,
          "first backup survives a replacement failure");
    CHECK(SetFileAttributesW(config_path, FILE_ATTRIBUTE_NORMAL),
          "configuration read-only flag is restored");
    CHECK(SetFileAttributesW(backup_path, FILE_ATTRIBUTE_NORMAL),
          "backup read-only flag is cleared for cleanup");

    CHECK(CreateDirectoryW(blocked_path, NULL), "blocking directory is created");
    CHECK(!launcher_settings_save(blocked_path, &settings, error_message, 256),
          "settings write fails when destination is a directory");

    CHECK(RemoveDirectoryW(blocked_path), "blocking directory is removed");
    CHECK(DeleteFileW(config_path), "failure-test config is removed");
    CHECK(DeleteFileW(backup_path), "failure-test backup is removed");
    CHECK(RemoveDirectoryW(directory), "failure-test directory is removed");
}

static void test_recommended_settings_reset(void)
{
    LauncherSettings settings;
    memset(&settings, 0xff, sizeof(settings));
    launcher_settings_recommended(&settings);
    CHECK(settings.display_mode == DISPLAY_MODE_BORDERLESS &&
          settings.width == 960 && settings.height == 720 &&
          settings.keep_aspect_ratio && settings.smooth_scaling &&
          settings.capture_mouse && settings.vertical_sync &&
          settings.center_window && !settings.quick_start,
          "recommended settings replace every managed option");
}

static void test_requested_action_parser(void)
{
    CHECK(launcher_requested_action_parse(1, NULL) == ACTION_AUTOMATIC,
          "no option selects automatic launch behaviour");
    CHECK(launcher_requested_action_parse(2, L"--settings") == ACTION_SETTINGS,
          "settings option is recognized");
    CHECK(launcher_requested_action_parse(2, L"--LAUNCH") == ACTION_LAUNCH,
          "launch option is case-insensitive");
    CHECK(launcher_requested_action_parse(2, L"--diagnose") == ACTION_DIAGNOSE,
          "diagnostics option is recognized");
    CHECK(launcher_requested_action_parse(2, L"--unknown") == ACTION_INVALID,
          "unknown option is rejected");
    CHECK(launcher_requested_action_parse(3, L"--launch") == ACTION_INVALID,
          "extra arguments are rejected");
    CHECK(launcher_requested_action_parse(2, NULL) == ACTION_INVALID,
          "missing option text is rejected");
}

int main(void)
{
    test_transform_preserves_unknown_content();
    test_transform_couples_mouse_mode_and_client_conversion();
    test_transform_rejects_duplicate();
    test_transform_accepts_utf8_bom();
    test_transform_preserves_lf_line_endings();
    test_transform_rejects_missing_and_utf16();
    test_validation();
    test_recommended_settings_reset();
    test_requested_action_parser();
    test_file_round_trip_and_backup();
    test_configuration_failure_paths();
    if (failures != 0) {
        fprintf(stderr, "%d configuration test(s) failed.\n", failures);
        return 1;
    }
    puts("All configuration tests passed.");
    return 0;
}
