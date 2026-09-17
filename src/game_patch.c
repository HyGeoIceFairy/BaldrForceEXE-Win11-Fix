#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

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
#define INPUT_UPDATE_ADDRESS 0x004A85B0U
#define ORIGINAL_MOUSE_UPDATE_ADDRESS 0x0040C750U
#define INPUT_UPDATE_CONTINUATION_ADDRESS 0x004A85B5U
#define INPUT_HELPER_CALL_RELATIVE_OFFSET 46U
#define INPUT_HELPER_RETURN_RELATIVE_OFFSET 51U

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
 * In the game's native fullscreen lifecycle, deactivation marks the game
 * inactive and then releases every DirectDraw object; activation recreates an
 * exclusive display mode. The inactive-state transition must remain intact so
 * the game loop suspends input while its window is not active. Windowed
 * launcher mode therefore skips only the incompatible DirectDraw teardown and
 * recreation while retaining the surrounding focus state handling.
 */
static const GameBytePatch desktop_display_lifecycle[] = {
    { { 0x004A8EC3U, { 0xE8, 0x38, 0xF0, 0xFF, 0xFF }, 5 },
      { 0x90, 0x90, 0x90, 0x90, 0x90 } },
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

/*
 * input_update_guard():
 *   Compare GetForegroundWindow() with the game's HWND. If another window is
 *   foreground, clear all keyboard/mouse state and return without polling.
 *   Otherwise execute the displaced mouse-update call and rejoin the original
 *   input-update function. The function pointer is stored after the code so
 *   this helper does not depend on a particular USER32 load address.
 */
static const BYTE inactive_input_helper_template[GAME_INACTIVE_INPUT_HELPER_SIZE] = {
    0xFF, 0x15, 0x11, 0x11, 0x11, 0x11, 0x3B, 0x05,
    0x48, 0x39, 0x50, 0x00, 0x74, 0x20, 0x31, 0xC0,
    0xA3, 0x50, 0x3A, 0x50, 0x00, 0xA3, 0x54, 0x3A,
    0x50, 0x00, 0xA3, 0x64, 0x3A, 0x50, 0x00, 0x57,
    0xB9, 0x40, 0x00, 0x00, 0x00, 0xBF, 0x6C, 0x3A,
    0x50, 0x00, 0xF3, 0xAB, 0x5F, 0xC3, 0xE8, 0x22,
    0x22, 0x22, 0x22, 0xE9, 0x33, 0x33, 0x33, 0x33,
    0x44, 0x44, 0x44, 0x44
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

static BOOL build_inactive_input_helper(BYTE *helper,
                                        uintptr_t helper_address,
                                        uintptr_t foreground_function)
{
    uint32_t function_slot_address;
    uint32_t function_address;
    uint32_t relative_call;
    uint32_t relative_return;

    if (helper_address > UINT32_MAX - GAME_INACTIVE_INPUT_HELPER_SIZE ||
        foreground_function > UINT32_MAX) {
        SetLastError(ERROR_INVALID_ADDRESS);
        return FALSE;
    }
    function_slot_address =
        (uint32_t)helper_address + GAME_INACTIVE_INPUT_FUNCTION_SLOT_OFFSET;
    function_address = (uint32_t)foreground_function;
    relative_call = (uint32_t)ORIGINAL_MOUSE_UPDATE_ADDRESS -
                    ((uint32_t)helper_address +
                     INPUT_HELPER_CALL_RELATIVE_OFFSET + 5U);
    relative_return = (uint32_t)INPUT_UPDATE_CONTINUATION_ADDRESS -
                      ((uint32_t)helper_address +
                       INPUT_HELPER_RETURN_RELATIVE_OFFSET + 5U);

    memcpy(helper, inactive_input_helper_template,
           sizeof(inactive_input_helper_template));
    memcpy(helper + 2U, &function_slot_address, sizeof(function_slot_address));
    memcpy(helper + INPUT_HELPER_CALL_RELATIVE_OFFSET + 1U,
           &relative_call, sizeof(relative_call));
    memcpy(helper + INPUT_HELPER_RETURN_RELATIVE_OFFSET + 1U,
           &relative_return, sizeof(relative_return));
    memcpy(helper + GAME_INACTIVE_INPUT_FUNCTION_SLOT_OFFSET,
           &function_address, sizeof(function_address));
    return TRUE;
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

BOOL game_inactive_input_patch_transform(BYTE *image, size_t image_size,
                                         uintptr_t image_base,
                                         uintptr_t helper_address,
                                         uintptr_t foreground_function,
                                         BYTE *helper, size_t helper_size)
{
    static const GameCodeSignature input_update = {
        INPUT_UPDATE_ADDRESS, { 0xE8, 0x9B, 0x41, 0xF6, 0xFF }, 5
    };
    size_t input_offset;
    BYTE jump[6];

    if (image == NULL || helper == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (helper_size < sizeof(inactive_input_helper_template)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (!validate_image_signature(image, image_size, image_base,
                                  &input_update, &input_offset) ||
        !make_relative_call(INPUT_UPDATE_ADDRESS, helper_address, jump) ||
        !build_inactive_input_helper(helper, helper_address,
                                     foreground_function)) {
        return FALSE;
    }
    jump[0] = 0xE9;
    memcpy(image + input_offset, jump, input_update.size);
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

static uintptr_t find_remote_module_base(DWORD process_id,
                                         const WCHAR *module_name)
{
    MODULEENTRY32W entry;
    HANDLE snapshot;
    uintptr_t result = 0;

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                       process_id);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, module_name) == 0) {
                result = (uintptr_t)entry.modBaseAddr;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

static uintptr_t resolve_remote_export(HANDLE process, uintptr_t module_base,
                                       const char *export_name)
{
    IMAGE_DOS_HEADER dos_header;
    IMAGE_NT_HEADERS32 nt_headers;
    IMAGE_EXPORT_DIRECTORY exports;
    DWORD *name_rvas = NULL;
    WORD *ordinals = NULL;
    DWORD *function_rvas = NULL;
    uintptr_t result = 0;
    size_t names_size;
    size_t ordinals_size;
    size_t functions_size;
    DWORD index;

    if (!read_exact(process, module_base, &dos_header, sizeof(dos_header)) ||
        dos_header.e_magic != IMAGE_DOS_SIGNATURE ||
        !read_exact(process, module_base + (uintptr_t)dos_header.e_lfanew,
                    &nt_headers, sizeof(nt_headers)) ||
        nt_headers.Signature != IMAGE_NT_SIGNATURE ||
        nt_headers.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        SetLastError(ERROR_BAD_EXE_FORMAT);
        return 0;
    }
    {
        const IMAGE_DATA_DIRECTORY *directory =
            &nt_headers.OptionalHeader
                 .DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (directory->VirtualAddress == 0 ||
            !read_exact(process, module_base + directory->VirtualAddress,
                        &exports, sizeof(exports))) {
            SetLastError(ERROR_PROC_NOT_FOUND);
            return 0;
        }
    }
    if (exports.NumberOfNames == 0 || exports.NumberOfFunctions == 0) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return 0;
    }
    names_size = (size_t)exports.NumberOfNames * sizeof(DWORD);
    ordinals_size = (size_t)exports.NumberOfNames * sizeof(WORD);
    functions_size = (size_t)exports.NumberOfFunctions * sizeof(DWORD);
    name_rvas = HeapAlloc(GetProcessHeap(), 0, names_size);
    ordinals = HeapAlloc(GetProcessHeap(), 0, ordinals_size);
    function_rvas = HeapAlloc(GetProcessHeap(), 0, functions_size);
    if (name_rvas == NULL || ordinals == NULL || function_rvas == NULL) {
        SetLastError(ERROR_OUTOFMEMORY);
        goto cleanup;
    }
    if (!read_exact(process, module_base + exports.AddressOfNames,
                    name_rvas, names_size) ||
        !read_exact(process, module_base + exports.AddressOfNameOrdinals,
                    ordinals, ordinals_size) ||
        !read_exact(process, module_base + exports.AddressOfFunctions,
                    function_rvas, functions_size)) {
        goto cleanup;
    }
    for (index = 0; index < exports.NumberOfNames; ++index) {
        char name[128];
        SIZE_T received = 0;

        ZeroMemory(name, sizeof(name));
        if (!ReadProcessMemory(process,
                               (LPCVOID)(module_base + name_rvas[index]), name,
                               sizeof(name) - 1U, &received) ||
            received == 0) {
            continue;
        }
        name[sizeof(name) - 1U] = '\0';
        if (strcmp(name, export_name) == 0 &&
            ordinals[index] < exports.NumberOfFunctions) {
            result = module_base + function_rvas[ordinals[index]];
            break;
        }
    }
    if (result == 0) {
        SetLastError(ERROR_PROC_NOT_FOUND);
    }

cleanup:
    if (function_rvas != NULL) {
        HeapFree(GetProcessHeap(), 0, function_rvas);
    }
    if (ordinals != NULL) {
        HeapFree(GetProcessHeap(), 0, ordinals);
    }
    if (name_rvas != NULL) {
        HeapFree(GetProcessHeap(), 0, name_rvas);
    }
    return result;
}

BOOL game_inactive_input_patch_apply(HANDLE process)
{
    static const BYTE expected[] = { 0xE8, 0x9B, 0x41, 0xF6, 0xFF };
    BYTE original[sizeof(expected)];
    BYTE replacement[6];
    BYTE helper[GAME_INACTIVE_INPUT_HELPER_SIZE];
    LPVOID remote_helper = NULL;
    uintptr_t user32_base;
    uintptr_t foreground_function;
    DWORD old_protection = 0;
    DWORD error_code;
    BOOL entry_attempted = FALSE;

    if (process == NULL || process == INVALID_HANDLE_VALUE) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!read_exact(process, INPUT_UPDATE_ADDRESS, original,
                    sizeof(original)) ||
        memcmp(original, expected, sizeof(expected)) != 0) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    user32_base = find_remote_module_base(GetProcessId(process), L"USER32.DLL");
    if (user32_base == 0) {
        if (GetLastError() == ERROR_SUCCESS) {
            SetLastError(ERROR_MOD_NOT_FOUND);
        }
        return FALSE;
    }
    foreground_function =
        resolve_remote_export(process, user32_base, "GetForegroundWindow");
    if (foreground_function == 0) {
        return FALSE;
    }
    remote_helper = VirtualAllocEx(process, NULL, sizeof(helper),
                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote_helper == NULL) {
        return FALSE;
    }
    if (!build_inactive_input_helper(helper, (uintptr_t)remote_helper,
                                     foreground_function) ||
        !make_relative_call(INPUT_UPDATE_ADDRESS, (uintptr_t)remote_helper,
                            replacement)) {
        error_code = GetLastError();
        goto rollback;
    }
    replacement[0] = 0xE9;
    if (!write_exact(process, (uintptr_t)remote_helper, helper,
                     sizeof(helper)) ||
        !VirtualProtectEx(process, remote_helper, sizeof(helper),
                          PAGE_EXECUTE_READ, &old_protection) ||
        !FlushInstructionCache(process, remote_helper, sizeof(helper))) {
        error_code = GetLastError();
        goto rollback;
    }
    entry_attempted = TRUE;
    if (!write_exact(process, INPUT_UPDATE_ADDRESS, replacement,
                     sizeof(original)) ||
        !FlushInstructionCache(process, (LPCVOID)INPUT_UPDATE_ADDRESS,
                               sizeof(original))) {
        error_code = GetLastError();
        goto rollback;
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;

rollback:
    if (!entry_attempted ||
        (write_exact(process, INPUT_UPDATE_ADDRESS, original,
                     sizeof(original)) &&
         FlushInstructionCache(process, (LPCVOID)INPUT_UPDATE_ADDRESS,
                               sizeof(original)))) {
        VirtualFreeEx(process, remote_helper, 0, MEM_RELEASE);
    }
    SetLastError(error_code);
    return FALSE;
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
