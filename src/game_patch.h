#ifndef BALDRFORCE_GAME_PATCH_H
#define BALDRFORCE_GAME_PATCH_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stddef.h>
#include <stdint.h>

#define GAME_WINDOWED_MOUSE_HELPER_SIZE 152U
#define GAME_WINDOWED_MOUSE_LOGICAL_TO_SCREEN_OFFSET 85U

BOOL game_desktop_display_patch_transform(BYTE *image, size_t image_size,
                                          uintptr_t image_base);
BOOL game_desktop_display_patch_apply(HANDLE process);
BOOL game_windowed_mouse_patch_transform(BYTE *image, size_t image_size,
                                         uintptr_t image_base,
                                         uintptr_t helper_address,
                                         unsigned int client_width,
                                         unsigned int client_height,
                                         BOOL keep_aspect_ratio,
                                         BYTE *helper, size_t helper_size);
BOOL game_windowed_mouse_patch_apply(HANDLE process,
                                     unsigned int client_width,
                                     unsigned int client_height,
                                     BOOL keep_aspect_ratio);

#endif
