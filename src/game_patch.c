#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "game_patch.h"

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))
#define VIEWPORT_LEFT_OFFSET 26U
#define VIEWPORT_WIDTH_OFFSET 38U
#define VIEWPORT_TOP_OFFSET 50U
#define VIEWPORT_HEIGHT_OFFSET 62U
#define VIEWPORT_WIDTH_REVERSE_OFFSET 94U
#define VIEWPORT_LEFT_REVERSE_OFFSET 107U
#define VIEWPORT_HEIGHT_REVERSE_OFFSET 116U
#define VIEWPORT_TOP_REVERSE_OFFSET 129U

typedef struct GameCodeSignature {
    uintptr_t address;
    BYTE expected[6];
    size_t size;
} GameCodeSignature;

typedef struct GameCallPatch {
    GameCodeSignature signature;
    size_t helper_offset;
} GameCallPatch;

typedef struct GameBytePatch {
    GameCodeSignature signature;
    BYTE replacement[6];
} GameBytePatch;

/*
 * The game skips its Win32 coordinate conversions in its native fullscreen
 * DirectDraw mode. dgVoodoo still presents that mode in a desktop window, so
 * the windowed launcher patch must remove these skips and route both
 * coordinate directions through the matching viewport-aware helpers.
 */
static const GameCodeSignature windowed_mouse_branches[] = {
    { 0x0040C765U, { 0x75, 0x12 }, 2 },
    { 0x00412042U, { 0x75, 0x11 }, 2 },
    { 0x004308B5U, { 0x75, 0x12 }, 2 }
};

static const GameCallPatch windowed_mouse_calls[] = {
    { { 0x0040C773U, { 0xFF, 0x15, 0x9C, 0xD2, 0x4B, 0x00 }, 6 }, 0 },
    { { 0x0041204FU, { 0xFF, 0x15, 0x9C, 0xD2, 0x4B, 0x00 }, 6 }, 0 },
    { { 0x004308C3U, { 0xFF, 0x15, 0x14, 0xD2, 0x4B, 0x00 }, 6 },
      GAME_WINDOWED_MOUSE_LOGICAL_TO_SCREEN_OFFSET }
};

/*
 * In the game's native fullscreen lifecycle, deactivation releases every
 * DirectDraw object and activation recreates an exclusive display mode. That
 * conflicts with dgVoodoo's desktop-window host and leaves the game with null
 * surfaces after minimize/restore. Windowed launcher mode keeps the existing
 * wrapper-owned surfaces alive and skips the incompatible recreation path.
 */
static const GameBytePatch desktop_display_lifecycle[] = {
    { { 0x004A8EADU, { 0x74, 0x19 }, 2 },
      { 0xEB, 0x19 } },
    { { 0x004A8F67U, { 0x0F, 0x84, 0xB8, 0x00, 0x00, 0x00 }, 6 },
      { 0xE9, 0xB9, 0x00, 0x00, 0x00, 0x90 } }
};

/*
 * Two reviewed stdcall x86 helpers encoded for the supported 32-bit game.
 *
 * screen_to_logical(hwnd, point):
 *   ScreenToClient, then client pixels -> the game's fixed 640x480 space.
 * logical_to_screen(hwnd, point):
 *   640x480 -> client pixels, then ClientToScreen.
 *
 * Calls use this game's fixed USER32 IAT entries. The call-site signatures
 * above ensure the helpers are installed only in the supported executable.
 */
static const BYTE mouse_scale_helper_template[GAME_WINDOWED_MOUSE_HELPER_SIZE] = {
    0x55, 0x89, 0xE5, 0x56, 0x8B, 0x75, 0x0C, 0xFF,
    0x75, 0x0C, 0xFF, 0x75, 0x08, 0xFF, 0x15, 0x9C,
    0xD2, 0x4B, 0x00, 0x85, 0xC0, 0x74, 0x37, 0x8B,
    0x06, 0x2D, 0x11, 0x11, 0x11, 0x11, 0x69, 0xC0,
    0x80, 0x02, 0x00, 0x00, 0x99, 0xB9, 0x33, 0x33,
    0x33, 0x33, 0xF7, 0xF9, 0x89, 0x06, 0x8B, 0x46,
    0x04, 0x2D, 0x22, 0x22, 0x22, 0x22, 0x69, 0xC0,
    0xE0, 0x01, 0x00, 0x00, 0x99, 0xB9, 0x44, 0x44,
    0x44, 0x44, 0xF7, 0xF9, 0x89, 0x46, 0x04, 0xB8,
    0x01, 0x00, 0x00, 0x00, 0xEB, 0x02, 0x31, 0xC0,
    0x5E, 0x5D, 0xC2, 0x08, 0x00, 0x55, 0x89, 0xE5,
    0x56, 0x8B, 0x75, 0x0C, 0x69, 0x06, 0x33, 0x33,
    0x33, 0x33, 0x99, 0xB9, 0x80, 0x02, 0x00, 0x00,
    0xF7, 0xF9, 0x05, 0x11, 0x11, 0x11, 0x11, 0x89,
    0x06, 0x69, 0x46, 0x04, 0x44, 0x44, 0x44, 0x44,
    0x99, 0xB9, 0xE0, 0x01, 0x00, 0x00, 0xF7, 0xF9,
    0x05, 0x22, 0x22, 0x22, 0x22, 0x89, 0x46, 0x04,
    0x56, 0xFF, 0x75, 0x08, 0xFF, 0x15, 0x14, 0xD2,
    0x4B, 0x00, 0x5E, 0x5D, 0xC2, 0x08, 0x00, 0x90
};

static void write_int32(BYTE *buffer, size_t offset, LONG value)
{
    int32_t encoded = (int32_t)value;
    memcpy(buffer + offset, &encoded, sizeof(encoded));
}

static void build_mouse_scale_helper(BYTE *helper,
                                     LONG viewport_left, LONG viewport_top,
                                     LONG viewport_width, LONG viewport_height)
{
    memcpy(helper, mouse_scale_helper_template,
           sizeof(mouse_scale_helper_template));
    write_int32(helper, VIEWPORT_LEFT_OFFSET, viewport_left);
    write_int32(helper, VIEWPORT_WIDTH_OFFSET, viewport_width);
    write_int32(helper, VIEWPORT_TOP_OFFSET, viewport_top);
    write_int32(helper, VIEWPORT_HEIGHT_OFFSET, viewport_height);
    write_int32(helper, VIEWPORT_WIDTH_REVERSE_OFFSET, viewport_width);
    write_int32(helper, VIEWPORT_LEFT_REVERSE_OFFSET, viewport_left);
    write_int32(helper, VIEWPORT_HEIGHT_REVERSE_OFFSET, viewport_height);
    write_int32(helper, VIEWPORT_TOP_REVERSE_OFFSET, viewport_top);
}

static BOOL calculate_viewport(unsigned int client_width,
                               unsigned int client_height,
                               BOOL keep_aspect_ratio,
                               LONG *left, LONG *top,
                               LONG *width, LONG *height)
{
    uint64_t scaled_width;
    uint64_t scaled_height;

    if (client_width == 0 || client_height == 0 ||
        client_width > INT32_MAX || client_height > INT32_MAX) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *left = 0;
    *top = 0;
    *width = (LONG)client_width;
    *height = (LONG)client_height;
    if (!keep_aspect_ratio) {
        return TRUE;
    }
    scaled_width = (uint64_t)client_height * 4U / 3U;
    if (scaled_width <= client_width) {
        *width = (LONG)scaled_width;
        *left = ((LONG)client_width - *width) / 2;
    } else {
        scaled_height = (uint64_t)client_width * 3U / 4U;
        *height = (LONG)scaled_height;
        *top = ((LONG)client_height - *height) / 2;
    }
    return *width > 0 && *height > 0;
}

static BOOL image_range(uintptr_t address, size_t size,
                        uintptr_t image_base, size_t image_size,
                        size_t *offset)
{
    uintptr_t relative;

    if (address < image_base) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    relative = address - image_base;
    if (relative > SIZE_MAX || (size_t)relative > image_size ||
        size > image_size - (size_t)relative) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    *offset = (size_t)relative;
    return TRUE;
}

static BOOL validate_image_signature(const BYTE *image, size_t image_size,
                                     uintptr_t image_base,
                                     const GameCodeSignature *signature,
                                     size_t *offset)
{
    if (!image_range(signature->address, signature->size, image_base,
                     image_size, offset) ||
        memcmp(image + *offset, signature->expected, signature->size) != 0) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return TRUE;
}

static BOOL make_relative_call(uintptr_t call_address, uintptr_t target,
                               BYTE replacement[6])
{
    uint32_t relative;

    if (call_address > UINT32_MAX - 5U || target > UINT32_MAX) {
        SetLastError(ERROR_INVALID_ADDRESS);
        return FALSE;
    }
    relative = (uint32_t)target - ((uint32_t)call_address + 5U);
    replacement[0] = 0xE8;
    memcpy(replacement + 1, &relative, sizeof(relative));
    replacement[5] = 0x90;
    return TRUE;
}

BOOL game_desktop_display_patch_transform(BYTE *image, size_t image_size,
                                          uintptr_t image_base)
{
    size_t offsets[ARRAY_COUNT(desktop_display_lifecycle)];
    size_t index;

    if (image == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (index = 0; index < ARRAY_COUNT(desktop_display_lifecycle); ++index) {
        if (!validate_image_signature(
                image, image_size, image_base,
                &desktop_display_lifecycle[index].signature, &offsets[index])) {
            return FALSE;
        }
    }
    for (index = 0; index < ARRAY_COUNT(desktop_display_lifecycle); ++index) {
        memcpy(image + offsets[index],
               desktop_display_lifecycle[index].replacement,
               desktop_display_lifecycle[index].signature.size);
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL game_windowed_mouse_patch_transform(BYTE *image, size_t image_size,
                                         uintptr_t image_base,
                                         uintptr_t helper_address,
                                         unsigned int client_width,
                                         unsigned int client_height,
                                         BOOL keep_aspect_ratio,
                                         BYTE *helper, size_t helper_size)
{
    size_t branch_offsets[ARRAY_COUNT(windowed_mouse_branches)];
    size_t call_offsets[ARRAY_COUNT(windowed_mouse_calls)];
    BYTE call_replacements[ARRAY_COUNT(windowed_mouse_calls)][6];
    static const BYTE unconditional_conversion[] = { 0x90, 0x90 };
    LONG viewport_left;
    LONG viewport_top;
    LONG viewport_width;
    LONG viewport_height;
    size_t index;

    if (image == NULL || helper == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (helper_size < sizeof(mouse_scale_helper_template)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (helper_address > UINT32_MAX - sizeof(mouse_scale_helper_template)) {
        SetLastError(ERROR_INVALID_ADDRESS);
        return FALSE;
    }
    if (!calculate_viewport(client_width, client_height, keep_aspect_ratio,
                            &viewport_left, &viewport_top,
                            &viewport_width, &viewport_height)) {
        return FALSE;
    }

    for (index = 0; index < ARRAY_COUNT(windowed_mouse_branches); ++index) {
        if (!validate_image_signature(image, image_size, image_base,
                                      &windowed_mouse_branches[index],
                                      &branch_offsets[index])) {
            return FALSE;
        }
    }
    for (index = 0; index < ARRAY_COUNT(windowed_mouse_calls); ++index) {
        if (!validate_image_signature(image, image_size, image_base,
                                      &windowed_mouse_calls[index].signature,
                                      &call_offsets[index]) ||
            !make_relative_call(windowed_mouse_calls[index].signature.address,
                                helper_address +
                                    windowed_mouse_calls[index].helper_offset,
                                call_replacements[index])) {
            return FALSE;
        }
    }
    build_mouse_scale_helper(helper, viewport_left, viewport_top,
                             viewport_width, viewport_height);
    for (index = 0; index < ARRAY_COUNT(windowed_mouse_branches); ++index) {
        memcpy(image + branch_offsets[index], unconditional_conversion,
               sizeof(unconditional_conversion));
    }
    for (index = 0; index < ARRAY_COUNT(windowed_mouse_calls); ++index) {
        memcpy(image + call_offsets[index], call_replacements[index],
               sizeof(call_replacements[index]));
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static BOOL read_exact(HANDLE process, uintptr_t address,
                       void *buffer, SIZE_T size)
{
    SIZE_T received = 0;
    return ReadProcessMemory(process, (LPCVOID)address, buffer, size,
                             &received) && received == size;
}

static BOOL write_exact(HANDLE process, uintptr_t address,
                        const void *buffer, SIZE_T size)
{
    SIZE_T written = 0;
    return WriteProcessMemory(process, (LPVOID)address, buffer, size,
                              &written) && written == size;
}

BOOL game_desktop_display_patch_apply(HANDLE process)
{
    BYTE original[ARRAY_COUNT(desktop_display_lifecycle)][6];
    DWORD error_code;
    size_t attempted = 0;
    size_t index;

    if (process == NULL || process == INVALID_HANDLE_VALUE) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    for (index = 0; index < ARRAY_COUNT(desktop_display_lifecycle); ++index) {
        const GameCodeSignature *signature =
            &desktop_display_lifecycle[index].signature;
        if (!read_exact(process, signature->address, original[index],
                        signature->size) ||
            memcmp(original[index], signature->expected, signature->size) != 0) {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
    }
    for (index = 0; index < ARRAY_COUNT(desktop_display_lifecycle); ++index) {
        const GameBytePatch *patch = &desktop_display_lifecycle[index];
        attempted = index + 1;
        if (!write_exact(process, patch->signature.address,
                         patch->replacement, patch->signature.size) ||
            !FlushInstructionCache(process,
                                   (LPCVOID)patch->signature.address,
                                   patch->signature.size)) {
            error_code = GetLastError();
            goto rollback;
        }
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;

rollback:
    /* Best effort only: the caller terminates the still-suspended process. */
    for (index = 0; index < attempted; ++index) {
        const GameCodeSignature *signature =
            &desktop_display_lifecycle[index].signature;
        if (write_exact(process, signature->address, original[index],
                        signature->size)) {
            FlushInstructionCache(process, (LPCVOID)signature->address,
                                  signature->size);
        }
    }
    SetLastError(error_code);
    return FALSE;
}

BOOL game_windowed_mouse_patch_apply(HANDLE process,
                                     unsigned int client_width,
                                     unsigned int client_height,
                                     BOOL keep_aspect_ratio)
{
    BYTE original_branches[ARRAY_COUNT(windowed_mouse_branches)][2];
    BYTE original_calls[ARRAY_COUNT(windowed_mouse_calls)][6];
    BYTE call_replacements[ARRAY_COUNT(windowed_mouse_calls)][6];
    BYTE mouse_scale_helper[sizeof(mouse_scale_helper_template)];
    static const BYTE unconditional_conversion[] = { 0x90, 0x90 };
    LPVOID remote_helper = NULL;
    LONG viewport_left;
    LONG viewport_top;
    LONG viewport_width;
    LONG viewport_height;
    DWORD old_protection = 0;
    DWORD error_code;
    size_t attempted_branches = 0;
    size_t attempted_calls = 0;
    size_t index;

    if (process == NULL || process == INVALID_HANDLE_VALUE) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!calculate_viewport(client_width, client_height, keep_aspect_ratio,
                            &viewport_left, &viewport_top,
                            &viewport_width, &viewport_height)) {
        return FALSE;
    }
    build_mouse_scale_helper(mouse_scale_helper, viewport_left, viewport_top,
                             viewport_width, viewport_height);

    /* Validate every supported-version signature before the first write. */
    for (index = 0; index < ARRAY_COUNT(windowed_mouse_branches); ++index) {
        const GameCodeSignature *signature = &windowed_mouse_branches[index];
        if (!read_exact(process, signature->address, original_branches[index],
                        signature->size) ||
            memcmp(original_branches[index], signature->expected,
                   signature->size) != 0) {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
    }
    for (index = 0; index < ARRAY_COUNT(windowed_mouse_calls); ++index) {
        const GameCodeSignature *signature =
            &windowed_mouse_calls[index].signature;
        if (!read_exact(process, signature->address, original_calls[index],
                        signature->size) ||
            memcmp(original_calls[index], signature->expected,
                   signature->size) != 0) {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
    }
    remote_helper = VirtualAllocEx(process, NULL, sizeof(mouse_scale_helper),
                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote_helper == NULL) {
        return FALSE;
    }
    if ((uintptr_t)remote_helper > UINT32_MAX - sizeof(mouse_scale_helper)) {
        error_code = ERROR_INVALID_ADDRESS;
        goto rollback;
    }
    for (index = 0; index < ARRAY_COUNT(windowed_mouse_calls); ++index) {
        if (!make_relative_call(windowed_mouse_calls[index].signature.address,
                                (uintptr_t)remote_helper +
                                    windowed_mouse_calls[index].helper_offset,
                                call_replacements[index])) {
            error_code = GetLastError();
            goto rollback;
        }
    }
    if (!write_exact(process, (uintptr_t)remote_helper, mouse_scale_helper,
                     sizeof(mouse_scale_helper)) ||
        !VirtualProtectEx(process, remote_helper, sizeof(mouse_scale_helper),
                          PAGE_EXECUTE_READ, &old_protection) ||
        !FlushInstructionCache(process, remote_helper,
                               sizeof(mouse_scale_helper))) {
        error_code = GetLastError();
        goto rollback;
    }

    for (index = 0; index < ARRAY_COUNT(windowed_mouse_branches); ++index) {
        attempted_branches = index + 1;
        if (!write_exact(process, windowed_mouse_branches[index].address,
                         unconditional_conversion,
                         sizeof(unconditional_conversion)) ||
            !FlushInstructionCache(
                process, (LPCVOID)windowed_mouse_branches[index].address,
                sizeof(unconditional_conversion))) {
            error_code = GetLastError();
            goto rollback;
        }
    }
    for (index = 0; index < ARRAY_COUNT(windowed_mouse_calls); ++index) {
        attempted_calls = index + 1;
        if (!write_exact(process, windowed_mouse_calls[index].signature.address,
                         call_replacements[index],
                         sizeof(call_replacements[index])) ||
            !FlushInstructionCache(
                process, (LPCVOID)windowed_mouse_calls[index].signature.address,
                sizeof(call_replacements[index]))) {
            error_code = GetLastError();
            goto rollback;
        }
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;

rollback:
    {
        BOOL code_restored = TRUE;
        DWORD rollback_error = error_code;

        for (index = 0; index < attempted_calls; ++index) {
            if (!write_exact(process,
                             windowed_mouse_calls[index].signature.address,
                             original_calls[index],
                             sizeof(original_calls[index])) ||
                !FlushInstructionCache(
                    process,
                    (LPCVOID)windowed_mouse_calls[index].signature.address,
                    sizeof(original_calls[index]))) {
                code_restored = FALSE;
            }
        }
        for (index = 0; index < attempted_branches; ++index) {
            if (!write_exact(process, windowed_mouse_branches[index].address,
                             original_branches[index],
                             sizeof(original_branches[index])) ||
                !FlushInstructionCache(
                    process, (LPCVOID)windowed_mouse_branches[index].address,
                    sizeof(original_branches[index]))) {
                code_restored = FALSE;
            }
        }
        /* Do not free code that a failed rollback may still reference. */
        if (code_restored) {
            VirtualFreeEx(process, remote_helper, 0, MEM_RELEASE);
        }
        SetLastError(rollback_error);
        return FALSE;
    }
}
