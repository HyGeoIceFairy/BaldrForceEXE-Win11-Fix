#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

#include "../src/game_patch.h"

#define TEST_IMAGE_BASE 0x00400000U
#define TEST_IMAGE_SIZE 0x00104000U
#define TEST_HELPER_ADDRESS 0x10000000U
#define TEST_INPUT_HELPER_ADDRESS 0x10001000U

static int failures;
static BYTE test_image[TEST_IMAGE_SIZE];
static BYTE test_helper[GAME_WINDOWED_MOUSE_HELPER_SIZE];
static BYTE test_input_helper[GAME_INACTIVE_INPUT_HELPER_SIZE];

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s (line %d, error %lu)\n", \
                    message, __LINE__, (unsigned long)GetLastError()); \
            ++failures; \
        } \
    } while (0)

static size_t image_offset(uintptr_t address)
{
    return (size_t)(address - TEST_IMAGE_BASE);
}

static void initialize_supported_image(void)
{
    memset(test_image, 0xCC, sizeof(test_image));
    memset(test_helper, 0xCC, sizeof(test_helper));
    memset(test_input_helper, 0xCC, sizeof(test_input_helper));
    test_image[image_offset(0x0040C765U)] = 0x75;
    test_image[image_offset(0x0040C766U)] = 0x12;
    memcpy(test_image + image_offset(0x0040C773U),
           "\xFF\x15\x9C\xD2\x4B\x00", 6);
    test_image[image_offset(0x00412042U)] = 0x75;
    test_image[image_offset(0x00412043U)] = 0x11;
    memcpy(test_image + image_offset(0x0041204FU),
           "\xFF\x15\x9C\xD2\x4B\x00", 6);
    test_image[image_offset(0x004308B5U)] = 0x75;
    test_image[image_offset(0x004308B6U)] = 0x12;
    memcpy(test_image + image_offset(0x004308C3U),
           "\xFF\x15\x14\xD2\x4B\x00", 6);
    memcpy(test_image + image_offset(0x004A8EADU),
           "\x74\x19", 2);
    memcpy(test_image + image_offset(0x004A8EC3U),
           "\xE8\x38\xF0\xFF\xFF", 5);
    memcpy(test_image + image_offset(0x004A8F67U),
           "\x0F\x84\xB8\x00\x00\x00", 6);
    memcpy(test_image + image_offset(0x00401435U),
           "\x8B\x4C\x24\x14", 4);
    memcpy(test_image + image_offset(0x004A85B0U),
           "\xE8\x9B\x41\xF6\xFF", 5);
}

static int32_t read_relative_call(uintptr_t address)
{
    int32_t relative;
    memcpy(&relative, test_image + image_offset(address) + 1,
           sizeof(relative));
    return relative;
}

static uintptr_t call_target(uintptr_t address)
{
    return address + 5U + (intptr_t)read_relative_call(address);
}

static void test_inactive_input_filter_is_patched(void)
{
    int32_t function_slot;

    initialize_supported_image();

    CHECK(game_inactive_input_patch_transform(
              test_image, sizeof(test_image), TEST_IMAGE_BASE,
              TEST_INPUT_HELPER_ADDRESS, 0x12345678U,
              test_input_helper, sizeof(test_input_helper)),
          "supported input update entry transforms");
    CHECK(test_image[image_offset(0x004A85B0U)] == 0xE9,
          "input update entry jumps to the foreground guard");
    CHECK(call_target(0x004A85B0U) == TEST_INPUT_HELPER_ADDRESS,
          "input update entry targets the injected guard");
    CHECK(test_input_helper[0] == 0xFF &&
              test_input_helper[1] == 0x15 &&
              test_input_helper[6] == 0x3B &&
              test_input_helper[7] == 0x05 &&
              test_input_helper[12] == 0x74 &&
              test_input_helper[13] == 0x20,
          "input guard compares the foreground window with the game window");
    CHECK(test_input_helper[14] == 0x31 &&
              test_input_helper[16] == 0xA3 &&
              test_input_helper[32] == 0xB9 &&
              test_input_helper[37] == 0xBF &&
              test_input_helper[42] == 0xF3 &&
              test_input_helper[43] == 0xAB &&
              test_input_helper[45] == 0xC3,
          "inactive branch clears mouse and keyboard state before returning");
    CHECK(test_input_helper[46] == 0xE8 &&
              test_input_helper[51] == 0xE9,
          "foreground branch executes and rejoins the original input update");
    memcpy(&function_slot,
           test_input_helper + GAME_INACTIVE_INPUT_FUNCTION_SLOT_OFFSET,
           sizeof(function_slot));
    CHECK((uint32_t)function_slot == 0x12345678U,
          "input guard calls the supplied foreground-window function");
}

static int32_t helper_int32(size_t offset)
{
    int32_t value;
    memcpy(&value, test_helper + offset, sizeof(value));
    return value;
}

static void test_supported_image_is_patched(void)
{
    initialize_supported_image();

    CHECK(game_windowed_mouse_patch_transform(test_image, sizeof(test_image),
                                              TEST_IMAGE_BASE,
                                              TEST_HELPER_ADDRESS,
                                              960, 720, TRUE,
                                              test_helper, sizeof(test_helper)),
          "supported windowed mouse targets transform");
    CHECK(test_image[image_offset(0x0040C765U)] == 0x90 &&
          test_image[image_offset(0x0040C766U)] == 0x90 &&
          test_image[image_offset(0x00412042U)] == 0x90 &&
          test_image[image_offset(0x00412043U)] == 0x90 &&
          test_image[image_offset(0x004308B5U)] == 0x90 &&
          test_image[image_offset(0x004308B6U)] == 0x90,
          "windowed mouse conversion branches always reach the scaling helpers");
    CHECK(test_image[image_offset(0x00401435U)] == 0x8B &&
          test_image[image_offset(0x00401436U)] == 0x4C &&
          test_image[image_offset(0x00401437U)] == 0x24 &&
          test_image[image_offset(0x00401438U)] == 0x14,
          "mouse patch preserves the game's DirectDraw display mode");
    CHECK(test_image[image_offset(0x0040C773U)] == 0xE8 &&
          test_image[image_offset(0x0040C778U)] == 0x90 &&
          call_target(0x0040C773U) == TEST_HELPER_ADDRESS,
          "global mouse reader calls the screen-to-logical helper");
    CHECK(test_image[image_offset(0x0041204FU)] == 0xE8 &&
          test_image[image_offset(0x00412054U)] == 0x90 &&
          call_target(0x0041204FU) == TEST_HELPER_ADDRESS,
          "UI mouse reader calls the screen-to-logical helper");
    CHECK(test_image[image_offset(0x004308C3U)] == 0xE8 &&
          test_image[image_offset(0x004308C8U)] == 0x90 &&
          call_target(0x004308C3U) ==
              TEST_HELPER_ADDRESS + GAME_WINDOWED_MOUSE_LOGICAL_TO_SCREEN_OFFSET,
          "mouse writer calls the logical-to-screen helper");
    CHECK(test_helper[0] == 0x55 && test_helper[13] == 0xFF &&
          test_helper[14] == 0x15 && test_helper[15] == 0x9C &&
          test_helper[GAME_WINDOWED_MOUSE_LOGICAL_TO_SCREEN_OFFSET] == 0x55 &&
          test_helper[140] == 0xFF && test_helper[141] == 0x15 &&
          test_helper[142] == 0x14,
          "helper preserves the expected API directions");
    CHECK(helper_int32(26) == 0 && helper_int32(38) == 960 &&
          helper_int32(50) == 0 && helper_int32(62) == 720 &&
          helper_int32(94) == 960 && helper_int32(107) == 0 &&
          helper_int32(116) == 720 && helper_int32(129) == 0,
          "both helpers use the configured client viewport");
}

static void test_desktop_display_lifecycle_is_patched(void)
{
    initialize_supported_image();

    CHECK(game_desktop_display_patch_transform(test_image, sizeof(test_image),
                                               TEST_IMAGE_BASE),
          "supported desktop display lifecycle transforms");
    CHECK(test_image[image_offset(0x004A8EADU)] == 0x74 &&
          test_image[image_offset(0x004A8EAEU)] == 0x19,
          "desktop mode preserves the inactive-state transition");
    CHECK(test_image[image_offset(0x004A8EC3U)] == 0x90 &&
          test_image[image_offset(0x004A8EC4U)] == 0x90 &&
          test_image[image_offset(0x004A8EC5U)] == 0x90 &&
          test_image[image_offset(0x004A8EC6U)] == 0x90 &&
          test_image[image_offset(0x004A8EC7U)] == 0x90 &&
          test_image[image_offset(0x004A8F67U)] == 0xE9 &&
          test_image[image_offset(0x004A8F68U)] == 0xB9 &&
          test_image[image_offset(0x004A8F69U)] == 0x00 &&
          test_image[image_offset(0x004A8F6AU)] == 0x00 &&
          test_image[image_offset(0x004A8F6BU)] == 0x00 &&
          test_image[image_offset(0x004A8F6CU)] == 0x90,
          "desktop mode skips only DirectDraw teardown and recreation");
}

static void test_aspect_ratio_viewport_is_encoded(void)
{
    initialize_supported_image();
    CHECK(game_windowed_mouse_patch_transform(test_image, sizeof(test_image),
                                              TEST_IMAGE_BASE,
                                              TEST_HELPER_ADDRESS,
                                              1280, 720, TRUE,
                                              test_helper, sizeof(test_helper)),
          "widescreen aspect-preserving viewport transforms");
    CHECK(helper_int32(26) == 160 && helper_int32(38) == 960 &&
          helper_int32(50) == 0 && helper_int32(62) == 720 &&
          helper_int32(94) == 960 && helper_int32(107) == 160 &&
          helper_int32(116) == 720 && helper_int32(129) == 0,
          "widescreen viewport removes centered pillarbox margins");

    initialize_supported_image();
    CHECK(game_windowed_mouse_patch_transform(test_image, sizeof(test_image),
                                              TEST_IMAGE_BASE,
                                              TEST_HELPER_ADDRESS,
                                              1280, 720, FALSE,
                                              test_helper, sizeof(test_helper)),
          "widescreen stretched viewport transforms");
    CHECK(helper_int32(26) == 0 && helper_int32(38) == 1280 &&
          helper_int32(50) == 0 && helper_int32(62) == 720 &&
          helper_int32(94) == 1280 && helper_int32(107) == 0,
          "stretching uses the complete client area");
}

static void test_mismatch_is_atomic(void)
{
    initialize_supported_image();
    test_image[image_offset(0x004308C8U)] = 0x01;

    CHECK(!game_windowed_mouse_patch_transform(test_image, sizeof(test_image),
                                               TEST_IMAGE_BASE,
                                               TEST_HELPER_ADDRESS,
                                               960, 720, TRUE,
                                               test_helper, sizeof(test_helper)),
          "unsupported mouse call is rejected");
    CHECK(GetLastError() == ERROR_INVALID_DATA,
          "unsupported mouse call reports invalid data");
    CHECK(test_image[image_offset(0x0040C765U)] == 0x75 &&
          test_image[image_offset(0x0040C766U)] == 0x12 &&
          test_image[image_offset(0x00412042U)] == 0x75 &&
          test_image[image_offset(0x00412043U)] == 0x11 &&
          test_image[image_offset(0x0040C773U)] == 0xFF &&
          test_image[image_offset(0x00401435U)] == 0x8B &&
          test_helper[0] == 0xCC,
          "no image or helper changes when later validation fails");
}

static void test_invalid_image_is_rejected(void)
{
    initialize_supported_image();

    CHECK(!game_windowed_mouse_patch_transform(NULL, sizeof(test_image),
                                               TEST_IMAGE_BASE,
                                               TEST_HELPER_ADDRESS,
                                               960, 720, TRUE,
                                               test_helper, sizeof(test_helper)),
          "null image is rejected");
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER,
          "null image reports invalid parameter");
    CHECK(!game_windowed_mouse_patch_transform(test_image, 16,
                                               TEST_IMAGE_BASE,
                                               TEST_HELPER_ADDRESS,
                                               960, 720, TRUE,
                                               test_helper, sizeof(test_helper)),
          "truncated image is rejected");
    CHECK(GetLastError() == ERROR_INVALID_DATA,
          "truncated image reports invalid data");
    CHECK(!game_windowed_mouse_patch_transform(test_image, sizeof(test_image),
                                               TEST_IMAGE_BASE,
                                               TEST_HELPER_ADDRESS,
                                               960, 720, TRUE,
                                               test_helper,
                                               sizeof(test_helper) - 1),
          "truncated helper is rejected");
    CHECK(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
          "truncated helper reports insufficient buffer");
    CHECK(!game_windowed_mouse_patch_transform(test_image, sizeof(test_image),
                                               TEST_IMAGE_BASE,
                                               TEST_HELPER_ADDRESS,
                                               0, 720, TRUE,
                                               test_helper, sizeof(test_helper)),
          "zero-width client area is rejected");
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER,
          "invalid client area reports invalid parameter");
}

int main(void)
{
    test_desktop_display_lifecycle_is_patched();
    test_inactive_input_filter_is_patched();
    test_supported_image_is_patched();
    test_aspect_ratio_viewport_is_encoded();
    test_mismatch_is_atomic();
    test_invalid_image_is_rejected();
    if (failures != 0) {
        fprintf(stderr, "%d game patch test(s) failed.\n", failures);
        return 1;
    }
    puts("All game patch tests passed.");
    return 0;
}
