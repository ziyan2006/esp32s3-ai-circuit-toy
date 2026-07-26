#include "pixel_assets.h"

#define EMBEDDED_BINARY(name) \
    extern const uint8_t _binary_##name##_bin_start[] asm( \
        "_binary_" #name "_bin_start")

#define SHOOTER_IMAGE(name, width, height)                         \
    EMBEDDED_BINARY(name);                                        \
    const lv_image_dsc_t img_##name = {                           \
        .header.cf = LV_COLOR_FORMAT_RGB565A8,                    \
        .header.magic = LV_IMAGE_HEADER_MAGIC,                    \
        .header.w = (width),                                      \
        .header.h = (height),                                     \
        .data_size = (uint32_t)((width) * (height) * 3U),         \
        .data = _binary_##name##_bin_start,                       \
    }

SHOOTER_IMAGE(player_ship, 48, 48);
SHOOTER_IMAGE(enemy_ship, 16, 16);
SHOOTER_IMAGE(laser_bullet, 3, 11);
SHOOTER_IMAGE(planet_00, 160, 160);
SHOOTER_IMAGE(planet_01, 160, 160);
SHOOTER_IMAGE(planet_02, 160, 160);
SHOOTER_IMAGE(planet_03, 160, 160);
SHOOTER_IMAGE(planet_04, 160, 160);
SHOOTER_IMAGE(planet_05, 160, 160);
SHOOTER_IMAGE(boss_ship, 192, 112);
SHOOTER_IMAGE(elite_bruiser, 72, 72);
SHOOTER_IMAGE(laser_drone, 48, 48);
