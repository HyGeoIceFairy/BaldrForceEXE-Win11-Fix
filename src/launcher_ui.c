#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>
#include <wctype.h>

#include "launcher_ui.h"
#include "resource.h"

#define PROJECT_RELEASES_URL L"https://github.com/HyGeoIceFairy/BaldrForceEXE-Win11-Fix/releases"

static const wchar_t *display_mode_text(DisplayMode mode)
{
    switch (mode) {
    case DISPLAY_MODE_WINDOWED:
        return L"窗口化（可选择分辨率）";
    case DISPLAY_MODE_BORDERLESS:
        return L"无边框全屏（推荐）";
    case DISPLAY_MODE_EXCLUSIVE:
        return L"传统独占全屏（兼容性因系统而异）";
    default:
        return L"未知";
    }
}

static void check_control(HWND dialog, int identifier, BOOL checked)
{
    CheckDlgButton(dialog, identifier, checked ? BST_CHECKED : BST_UNCHECKED);
}

static BOOL control_checked(HWND dialog, int identifier)
{
    return IsDlgButtonChecked(dialog, identifier) == BST_CHECKED;
}

static void update_mode_controls(HWND dialog)
{
    int selection = (int)SendDlgItemMessageW(dialog, IDC_DISPLAY_MODE,
                                              CB_GETCURSEL, 0, 0);
    BOOL windowed = selection == DISPLAY_MODE_WINDOWED;
    EnableWindow(GetDlgItem(dialog, IDC_RESOLUTION), windowed);
    EnableWindow(GetDlgItem(dialog, IDC_CENTER_WINDOW), windowed);
}

static void populate_controls(HWND dialog, const LauncherSettings *settings)
{
    static const wchar_t *resolutions[] = {
        L"640 x 480（原始大小）",
        L"800 x 600",
        L"960 x 720",
        L"1024 x 768",
        L"1280 x 960"
    };
    HWND modes = GetDlgItem(dialog, IDC_DISPLAY_MODE);
    HWND resolution = GetDlgItem(dialog, IDC_RESOLUTION);
    wchar_t size_text[64];
    size_t index;

    SendMessageW(modes, CB_RESETCONTENT, 0, 0);
    for (index = 0; index <= DISPLAY_MODE_EXCLUSIVE; ++index) {
        SendMessageW(modes, CB_ADDSTRING, 0, (LPARAM)display_mode_text((DisplayMode)index));
    }
    SendMessageW(modes, CB_SETCURSEL, settings->display_mode, 0);

    SendMessageW(resolution, CB_RESETCONTENT, 0, 0);
    for (index = 0; index < sizeof(resolutions) / sizeof(resolutions[0]); ++index) {
        SendMessageW(resolution, CB_ADDSTRING, 0, (LPARAM)resolutions[index]);
    }
    _snwprintf(size_text, sizeof(size_text) / sizeof(size_text[0]) - 1,
               L"%u x %u", settings->width, settings->height);
    size_text[sizeof(size_text) / sizeof(size_text[0]) - 1] = L'\0';
    SetWindowTextW(resolution, size_text);

    check_control(dialog, IDC_KEEP_ASPECT, settings->keep_aspect_ratio);
    check_control(dialog, IDC_SMOOTH_SCALING, settings->smooth_scaling);
    check_control(dialog, IDC_CAPTURE_MOUSE, settings->capture_mouse);
    check_control(dialog, IDC_VERTICAL_SYNC, settings->vertical_sync);
    check_control(dialog, IDC_CENTER_WINDOW, settings->center_window);
    check_control(dialog, IDC_QUICK_START, settings->quick_start);
    update_mode_controls(dialog);
}

static BOOL parse_resolution_text(const wchar_t *text,
                                  unsigned int *width,
                                  unsigned int *height)
{
    wchar_t *end;
    unsigned long parsed_width;
    unsigned long parsed_height;

    while (iswspace(*text)) {
        ++text;
    }
    parsed_width = wcstoul(text, &end, 10);
    if (end == text || parsed_width > UINT_MAX) {
        return FALSE;
    }
    while (iswspace(*end)) {
        ++end;
    }
    if (*end != L'x' && *end != L'X' && *end != L'×') {
        return FALSE;
    }
    text = end + 1;
    while (iswspace(*text)) {
        ++text;
    }
    parsed_height = wcstoul(text, &end, 10);
    if (end == text || parsed_height > UINT_MAX) {
        return FALSE;
    }
    while (iswspace(*end)) {
        ++end;
    }
    if (*end != L'\0' && wcscmp(end, L"（原始大小）") != 0) {
        return FALSE;
    }
    *width = (unsigned int)parsed_width;
    *height = (unsigned int)parsed_height;
    return TRUE;
}

static BOOL get_maximum_window_client_size(HWND dialog,
                                           unsigned int *maximum_width,
                                           unsigned int *maximum_height)
{
    MONITORINFO monitor_info;
    HMONITOR monitor = MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST);
    RECT nonclient = { 0, 0, 0, 0 };
    LONG work_width;
    LONG work_height;
    LONG nonclient_width;
    LONG nonclient_height;
    UINT dpi = GetDpiForWindow(dialog);

    ZeroMemory(&monitor_info, sizeof(monitor_info));
    monitor_info.cbSize = sizeof(monitor_info);
    if (!GetMonitorInfoW(monitor, &monitor_info)) {
        return FALSE;
    }
    if (dpi == 0) {
        dpi = 96;
    }
    if (!AdjustWindowRectExForDpi(&nonclient, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  FALSE, 0, dpi)) {
        return FALSE;
    }
    work_width = monitor_info.rcWork.right - monitor_info.rcWork.left;
    work_height = monitor_info.rcWork.bottom - monitor_info.rcWork.top;
    nonclient_width = nonclient.right - nonclient.left;
    nonclient_height = nonclient.bottom - nonclient.top;
    if (work_width <= nonclient_width || work_height <= nonclient_height) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    *maximum_width = (unsigned int)(work_width - nonclient_width);
    *maximum_height = (unsigned int)(work_height - nonclient_height);
    return TRUE;
}

static BOOL read_controls(HWND dialog, LauncherSettings *settings,
                          wchar_t *error_message, size_t error_message_count)
{
    wchar_t resolution_text[128];
    unsigned int maximum_width = 0;
    unsigned int maximum_height = 0;
    int selection = (int)SendDlgItemMessageW(dialog, IDC_DISPLAY_MODE,
                                              CB_GETCURSEL, 0, 0);

    if (selection < DISPLAY_MODE_WINDOWED || selection > DISPLAY_MODE_EXCLUSIVE) {
        wcsncpy(error_message, L"请选择显示模式。", error_message_count - 1);
        error_message[error_message_count - 1] = L'\0';
        return FALSE;
    }
    settings->display_mode = (DisplayMode)selection;
    GetDlgItemTextW(dialog, IDC_RESOLUTION, resolution_text,
                    (int)(sizeof(resolution_text) / sizeof(resolution_text[0])));
    if (!parse_resolution_text(resolution_text, &settings->width, &settings->height)) {
        wcsncpy(error_message, L"窗口分辨率格式无效，请输入例如 960 x 720。",
                error_message_count - 1);
        error_message[error_message_count - 1] = L'\0';
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    settings->keep_aspect_ratio = control_checked(dialog, IDC_KEEP_ASPECT);
    settings->smooth_scaling = control_checked(dialog, IDC_SMOOTH_SCALING);
    settings->capture_mouse = control_checked(dialog, IDC_CAPTURE_MOUSE);
    settings->vertical_sync = control_checked(dialog, IDC_VERTICAL_SYNC);
    settings->center_window = control_checked(dialog, IDC_CENTER_WINDOW);
    settings->quick_start = control_checked(dialog, IDC_QUICK_START);
    if (settings->display_mode == DISPLAY_MODE_WINDOWED &&
        !get_maximum_window_client_size(dialog, &maximum_width, &maximum_height)) {
        wcsncpy(error_message, L"无法确定当前显示器可容纳的最大窗口尺寸。",
                error_message_count - 1);
        error_message[error_message_count - 1] = L'\0';
        return FALSE;
    }
    return launcher_settings_validate(settings, maximum_width, maximum_height,
                                       error_message, error_message_count);
}

static void set_launch_controls_enabled(HWND dialog, BOOL enabled)
{
    static const int controls[] = {
        IDC_DISPLAY_MODE,
        IDC_RESOLUTION,
        IDC_KEEP_ASPECT,
        IDC_SMOOTH_SCALING,
        IDC_CAPTURE_MOUSE,
        IDC_VERTICAL_SYNC,
        IDC_CENTER_WINDOW,
        IDC_QUICK_START,
        IDC_RESET,
        IDC_OPEN_FOLDER,
        IDC_OPEN_GUIDE,
        IDC_OPEN_RELEASES,
        IDC_COPY_DIAGNOSTICS,
        IDOK,
        IDCANCEL
    };
    size_t index;

    for (index = 0; index < sizeof(controls) / sizeof(controls[0]); ++index) {
        EnableWindow(GetDlgItem(dialog, controls[index]), enabled);
    }
    if (enabled) {
        update_mode_controls(dialog);
    }
}

static void show_shell_error(HWND owner, const wchar_t *item)
{
    wchar_t message[512];
    _snwprintf(message, sizeof(message) / sizeof(message[0]) - 1,
               L"无法打开：%ls\n\n请确认文件存在，并检查系统默认打开方式。", item);
    message[sizeof(message) / sizeof(message[0]) - 1] = L'\0';
    MessageBoxW(owner, message, L"BALDR FORCE EXE 中文版启动器",
                MB_OK | MB_ICONERROR);
}

static void open_item(HWND owner, const wchar_t *item)
{
    HINSTANCE result = ShellExecuteW(owner, L"open", item, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        show_shell_error(owner, item);
    }
}

void launcher_build_diagnostics(const LauncherSettings *settings,
                                wchar_t *buffer,
                                size_t buffer_count)
{
    OSVERSIONINFOW version;
    WIN32_FILE_ATTRIBUTE_DATA game_data;
    ULARGE_INTEGER game_size;
    BOOL has_game;
    BOOL has_ddraw;
    BOOL has_config;
    BOOL has_sound;

    ZeroMemory(&version, sizeof(version));
    version.dwOSVersionInfoSize = sizeof(version);
    GetVersionExW(&version);
    has_game = GetFileAttributesExW(L"BaldrForce.exe", GetFileExInfoStandard,
                                    &game_data);
    game_size.QuadPart = 0;
    if (has_game) {
        game_size.HighPart = game_data.nFileSizeHigh;
        game_size.LowPart = game_data.nFileSizeLow;
    }
    has_ddraw = GetFileAttributesW(L"ddraw.dll") != INVALID_FILE_ATTRIBUTES;
    has_config = GetFileAttributesW(DGVOODOO_CONFIG_FILE) != INVALID_FILE_ATTRIBUTES;
    has_sound = GetFileAttributesW(L"Se\\Abort.wav") != INVALID_FILE_ATTRIBUTES;
    _snwprintf(buffer, buffer_count - 1,
               L"BALDR FORCE EXE Win11 Fix 1.1.1\r\n"
               L"Windows: %lu.%lu build %lu, %u-bit process\r\n"
               L"Game: %ls, size=%llu, expected=46252058\r\n"
               L"ddraw.dll: %ls; dgVoodoo.conf: %ls; Se\\Abort.wav: %ls\r\n"
               L"Mode: %ls; window=%u x %u; aspect=%ls; scaling=%ls\r\n"
               L"Mouse capture=%ls; VSync=%ls; center=%ls; quick start=%ls\r\n"
               L"Log: Start-BaldrForce.log",
               (unsigned long)version.dwMajorVersion,
               (unsigned long)version.dwMinorVersion,
               (unsigned long)version.dwBuildNumber,
               (unsigned int)(sizeof(void *) * 8),
               has_game ? L"found" : L"missing",
               (unsigned long long)game_size.QuadPart,
               has_ddraw ? L"found" : L"missing",
               has_config ? L"found" : L"missing",
               has_sound ? L"found" : L"missing",
               display_mode_text(settings->display_mode),
               settings->width, settings->height,
               settings->keep_aspect_ratio ? L"on" : L"off",
               settings->smooth_scaling ? L"smooth" : L"point",
               settings->capture_mouse ? L"on" : L"off",
               settings->vertical_sync ? L"on" : L"off",
               settings->center_window ? L"on" : L"off",
               settings->quick_start ? L"on" : L"off");
    buffer[buffer_count - 1] = L'\0';
}

static BOOL copy_text_to_clipboard(HWND owner, const wchar_t *text)
{
    size_t size = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL memory;
    wchar_t *destination;

    if (!OpenClipboard(owner)) {
        return FALSE;
    }
    EmptyClipboard();
    memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (memory == NULL) {
        CloseClipboard();
        return FALSE;
    }
    destination = (wchar_t *)GlobalLock(memory);
    if (destination == NULL) {
        GlobalFree(memory);
        CloseClipboard();
        return FALSE;
    }
    memcpy(destination, text, size);
    GlobalUnlock(memory);
    if (SetClipboardData(CF_UNICODETEXT, memory) == NULL) {
        GlobalFree(memory);
        CloseClipboard();
        return FALSE;
    }
    CloseClipboard();
    return TRUE;
}

static INT_PTR CALLBACK launcher_dialog_proc(HWND dialog, UINT message,
                                             WPARAM wparam, LPARAM lparam)
{
    LauncherDialogContext *context = (LauncherDialogContext *)
        GetWindowLongPtrW(dialog, DWLP_USER);

    switch (message) {
    case WM_INITDIALOG:
        context = (LauncherDialogContext *)lparam;
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)context);
        populate_controls(dialog, &context->settings);
        EnableWindow(GetDlgItem(dialog, IDOK), context->can_start);
        if (context->status_message != NULL && context->status_message[0] != L'\0') {
            SetDlgItemTextW(dialog, IDC_STATUS, context->status_message);
        } else if (context->imported_legacy) {
            SetDlgItemTextW(dialog, IDC_STATUS,
                            L"已读取旧版 dgVoodoo 显示设置。保存并启动后才会写入新版设置文件。\r\n"
                            L"未列出的高级配置会原样保留；无边框全屏在现代 Windows 上更稳定。");
        } else {
            SetDlgItemTextW(dialog, IDC_STATUS,
                            L"无边框全屏为推荐模式。窗口分辨率只改变显示尺寸，不提高原始素材精度。\r\n"
                            L"未列出的高级配置会原样保留；按住 Shift 启动可重新进入本面板。");
        }
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_DISPLAY_MODE:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                update_mode_controls(dialog);
            }
            return TRUE;
        case IDC_RESET:
            launcher_settings_recommended(&context->settings);
            populate_controls(dialog, &context->settings);
            SetDlgItemTextW(dialog, IDC_STATUS,
                            L"已在面板中恢复推荐设置；点击“保存并启动”后才会写入文件。");
            return TRUE;
        case IDC_OPEN_FOLDER:
            open_item(dialog, L".");
            return TRUE;
        case IDC_OPEN_GUIDE:
            open_item(dialog, L"USER_GUIDE.zh-CN.txt");
            return TRUE;
        case IDC_OPEN_RELEASES:
            open_item(dialog, PROJECT_RELEASES_URL);
            return TRUE;
        case IDC_COPY_DIAGNOSTICS: {
            LauncherSettings settings = context->settings;
            wchar_t diagnostics[2048];
            wchar_t error_message[256];
            if (!read_controls(dialog, &settings, error_message,
                               sizeof(error_message) / sizeof(error_message[0]))) {
                settings = context->settings;
            }
            launcher_build_diagnostics(&settings, diagnostics,
                                       sizeof(diagnostics) / sizeof(diagnostics[0]));
            if (copy_text_to_clipboard(dialog, diagnostics)) {
                SetDlgItemTextW(dialog, IDC_STATUS, L"诊断摘要已复制到剪贴板（不会自动上传）。");
            } else {
                MessageBoxW(dialog, L"无法访问剪贴板。", L"复制诊断",
                            MB_OK | MB_ICONERROR);
            }
            return TRUE;
        }
        case IDOK: {
            wchar_t error_message[256];
            if (!read_controls(dialog, &context->settings, error_message,
                               sizeof(error_message) / sizeof(error_message[0]))) {
                MessageBoxW(dialog, error_message, L"设置无效",
                            MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(dialog, IDC_RESOLUTION));
                return TRUE;
            }
            if (context->start_callback != NULL) {
                set_launch_controls_enabled(dialog, FALSE);
                SetDlgItemTextW(dialog, IDC_STATUS,
                                L"正在保存设置并启动游戏，请稍候……");
                UpdateWindow(dialog);
                if (!context->start_callback(&context->settings,
                                             context->start_user_data)) {
                    set_launch_controls_enabled(dialog, TRUE);
                    EnableWindow(GetDlgItem(dialog, IDOK), context->can_start);
                    SetDlgItemTextW(dialog, IDC_STATUS,
                                    L"启动失败。错误详情已显示并写入本地阶段日志；"
                                    L"可修改设置后重试，或取消退出。");
                    return TRUE;
                }
            }
            EndDialog(dialog, LAUNCHER_DIALOG_START);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, LAUNCHER_DIALOG_CANCELLED);
            return TRUE;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        EndDialog(dialog, LAUNCHER_DIALOG_CANCELLED);
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

LauncherDialogResult launcher_show_dialog(HINSTANCE instance,
                                          LauncherDialogContext *context)
{
    INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_LAUNCHER),
                                     NULL, launcher_dialog_proc,
                                     (LPARAM)context);
    if (result == -1) {
        MessageBoxW(NULL, L"无法创建启动设置窗口。",
                    L"BALDR FORCE EXE 中文版启动器",
                    MB_OK | MB_ICONERROR);
        return LAUNCHER_DIALOG_CANCELLED;
    }
    return (LauncherDialogResult)result;
}
