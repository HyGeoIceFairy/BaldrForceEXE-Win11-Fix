#ifndef BALDRFORCE_LAUNCHER_CONFIG_H
#define BALDRFORCE_LAUNCHER_CONFIG_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#define LAUNCHER_SETTINGS_FILE L"Start-BaldrForce.ini"
#define DGVOODOO_CONFIG_FILE L"dgVoodoo.conf"
#define DGVOODOO_BACKUP_FILE L"dgVoodoo.conf.baldrforce-backup"

typedef enum DisplayMode {
    DISPLAY_MODE_WINDOWED = 0,
    DISPLAY_MODE_BORDERLESS = 1,
    DISPLAY_MODE_EXCLUSIVE = 2
} DisplayMode;

typedef struct LauncherSettings {
    DisplayMode display_mode;
    unsigned int width;
    unsigned int height;
    BOOL keep_aspect_ratio;
    BOOL smooth_scaling;
    BOOL capture_mouse;
    BOOL vertical_sync;
    BOOL center_window;
    BOOL quick_start;
} LauncherSettings;

typedef enum RequestedAction {
    ACTION_AUTOMATIC,
    ACTION_SETTINGS,
    ACTION_LAUNCH,
    ACTION_DIAGNOSE,
    ACTION_INVALID
} RequestedAction;

void launcher_settings_recommended(LauncherSettings *settings);
RequestedAction launcher_requested_action_parse(int argument_count,
                                                const wchar_t *argument);
BOOL launcher_settings_validate(const LauncherSettings *settings,
                                unsigned int maximum_width,
                                unsigned int maximum_height,
                                wchar_t *error_message,
                                size_t error_message_count);
BOOL launcher_settings_load(const wchar_t *settings_path,
                            const wchar_t *dgvoodoo_path,
                            LauncherSettings *settings,
                            BOOL *imported_legacy,
                            wchar_t *error_message,
                            size_t error_message_count);
BOOL launcher_settings_save(const wchar_t *settings_path,
                            const LauncherSettings *settings,
                            wchar_t *error_message,
                            size_t error_message_count);
BOOL dgvoodoo_config_apply(const wchar_t *config_path,
                          const wchar_t *backup_path,
                          const LauncherSettings *settings,
                          wchar_t *error_message,
                          size_t error_message_count);

/* Exposed for the native configuration tests. The caller frees *output. */
BOOL dgvoodoo_config_transform(const char *input,
                              size_t input_size,
                              const LauncherSettings *settings,
                              char **output,
                              size_t *output_size,
                              wchar_t *error_message,
                              size_t error_message_count);

#endif
