#ifndef SHOOTER_RENDERER_LVGL_H
#define SHOOTER_RENDERER_LVGL_H

#include "lvgl.h"
#include "shooter_core.h"

typedef struct {
    int16_t x;
    int16_t y;
    uint8_t speed;
    lv_obj_t *object;
} shooter_star_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t speed;
    uint8_t image_index;
    lv_obj_t *object;
} shooter_bg_planet_t;

typedef struct {
    lv_obj_t *arena;
    lv_obj_t *hint;
    lv_obj_t *hud_hp;
    lv_obj_t *hud_device;
    lv_obj_t *hud_wave;
    lv_obj_t *hud_phase;
    lv_obj_t *upgrade_status;
    lv_obj_t *damage_overlay;
    lv_obj_t *danger_overlay;
    lv_obj_t *device_banner;
    lv_obj_t *player_marker;
    lv_obj_t *shield_aura;
    lv_obj_t *beam_overlay;
    lv_obj_t *bullet_markers[SHOOTER_MAX_PLAYER_BULLETS];
    lv_obj_t *enemy_bullet_markers[SHOOTER_MAX_ENEMY_BULLETS];
    lv_obj_t *enemy_markers[SHOOTER_MAX_ENEMIES];
    lv_obj_t *enemy_warning_lines[SHOOTER_MAX_ENEMIES];
    lv_obj_t *explosion_markers[SHOOTER_MAX_EXPLOSIONS];
    shooter_star_t stars[24];
    shooter_bg_planet_t planet;
    char cached_hint[128];
    int16_t cached_hp;
    int16_t cached_damage;
    uint16_t cached_max_hp;
    uint16_t cached_device_percent;
    uint16_t cached_wave_completed;
    uint16_t cached_wave_total;
    uint8_t cached_phase;
    uint8_t cached_projectiles;
    uint8_t cached_device_unlocked;
    uint8_t rendered_archetypes[SHOOTER_MAX_ENEMIES];
} shooter_renderer_lvgl_t;

void shooter_renderer_lvgl_create(shooter_renderer_lvgl_t *renderer, lv_obj_t *parent);
void shooter_renderer_lvgl_update(shooter_renderer_lvgl_t *renderer, const shooter_core_t *core);

#endif
