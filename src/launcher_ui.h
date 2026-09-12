#ifndef BALDRFORCE_LAUNCHER_UI_H
#define BALDRFORCE_LAUNCHER_UI_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "launcher_config.h"

typedef enum LauncherDialogResult {
    LAUNCHER_DIALOG_CANCELLED = 0,
    LAUNCHER_DIALOG_START = 1
} LauncherDialogResult;

typedef BOOL (*LauncherStartCallback)(const LauncherSettings *settings,
                                      void *user_data);

typedef struct LauncherDialogContext {
    LauncherSettings settings;
    BOOL imported_legacy;
    BOOL can_start;
    const wchar_t *status_message;
    LauncherStartCallback start_callback;
    void *start_user_data;
} LauncherDialogContext;

LauncherDialogResult launcher_show_dialog(HINSTANCE instance,
                                          LauncherDialogContext *context);

void launcher_build_diagnostics(const LauncherSettings *settings,
                                wchar_t *buffer,
                                size_t buffer_count);

#endif
