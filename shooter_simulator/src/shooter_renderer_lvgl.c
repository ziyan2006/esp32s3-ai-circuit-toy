#include "shooter_renderer_lvgl.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "pixel_assets.h"

#ifndef SHOOTER_PLANET_COUNT
#define SHOOTER_PLANET_COUNT 10U
#endif

#if SHOOTER_PLANET_COUNT < 1 || SHOOTER_PLANET_COUNT > 10
#error "SHOOTER_PLANET_COUNT must be in the range 1..10"
#endif

static const lv_image_dsc_t *const s_planet_images[] = {
    &img_planet_00, &img_planet_01, &img_planet_02, &img_planet_03, &img_planet_04,
    &img_planet_05,
#if SHOOTER_PLANET_COUNT > 6
    &img_planet_06,
#endif
#if SHOOTER_PLANET_COUNT > 7
    &img_planet_07,
#endif
#if SHOOTER_PLANET_COUNT > 8
    &img_planet_08,
#endif
#if SHOOTER_PLANET_COUNT > 9
    &img_planet_09,
#endif
};

static void style_hud_label(lv_obj_t *label, lv_coord_t width, lv_coord_t x)
{
    lv_obj_set_width(label, width);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, 8);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF7F1E3), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, 0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
}

static void set_hidden(lv_obj_t *object, bool hidden)
{
    if (hidden) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}

static const char *hazard_text(const shooter_core_t *core)
{
    switch (core->boss_hazard_kind) {
    case SHOOTER_HAZARD_TARGET_COLUMN:
        return core->boss_hazard_active_ms > 0U ? "标记生效" : "标记预警";
    case SHOOTER_HAZARD_EDGE_WALLS:
        return core->boss_hazard_active_ms > 0U ? "边墙生效" : "边墙预警";
    case SHOOTER_HAZARD_BOTTOM_SWEEP:
        return core->boss_hazard_active_ms > 0U ? "横扫生效" : "横扫预警";
    case SHOOTER_HAZARD_GATE_PAIR:
        return core->boss_hazard_active_ms > 0U ? "闸门生效" : "闸门预警";
    case SHOOTER_HAZARD_CENTER_WALL:
        return core->boss_hazard_active_ms > 0U ? "中墙生效" : "中墙预警";
    case SHOOTER_HAZARD_NONE:
    default:
        return "安全";
    }
}

static void boss_status_text(char *buffer, size_t size, const shooter_core_t *core)
{
    if (core->boss_definition == NULL) {
        lv_snprintf(buffer, size, "%s", shooter_core_phase_name(core->phase));
        return;
    }

    for (uint16_t i = 0U; i < SHOOTER_MAX_ENEMIES; ++i) {
        const shooter_enemy_t *enemy = &core->enemies[i];
        if (enemy->active && enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_BOSS) {
            lv_snprintf(buffer, size, "首领 %d/%u  %s  %s",
                        (int)enemy->hp,
                        (unsigned)enemy->max_hp,
                        core->boss_vulnerable ? "破绽" : "装甲",
                        hazard_text(core));
            return;
        }
    }

    lv_snprintf(buffer, size, "首领待机");
}

void shooter_renderer_lvgl_create(shooter_renderer_lvgl_t *renderer, lv_obj_t *parent)
{
    if (renderer == NULL) {
        return;
    }

    renderer->arena = lv_obj_create(parent);
    lv_obj_set_size(renderer->arena, 880, 420);
    lv_obj_set_style_radius(renderer->arena, 16, 0);
    lv_obj_set_style_bg_color(renderer->arena, lv_color_hex(0x0B1D3A), 0);
    lv_obj_set_style_border_width(renderer->arena, 2, 0);
    lv_obj_set_style_border_color(renderer->arena, lv_color_hex(0x00D2D3), 0);
    lv_obj_set_style_pad_all(renderer->arena, 10, 0);
    lv_obj_clear_flag(renderer->arena, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0U; i < 24U; ++i) {
        shooter_star_t *star = &renderer->stars[i];
        star->x = (int16_t)(rand() % 880);
        star->y = (int16_t)(rand() % 420);
        star->speed = i < 8U ? 1U : (i < 16U ? 2U : 4U);
        star->object = lv_obj_create(renderer->arena);
        lv_obj_set_size(star->object, star->speed == 4U ? 2 : 1, star->speed == 4U ? 2 : 1);
        lv_obj_set_style_bg_color(star->object, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(star->object, star->speed == 4U ? LV_OPA_80 : LV_OPA_40, 0);
        lv_obj_set_style_border_width(star->object, 0, 0);
        lv_obj_set_style_radius(star->object, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(star->object, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(star->object, star->x, star->y);
    }

    renderer->planet.image_index = (uint8_t)(rand() % SHOOTER_PLANET_COUNT);
    renderer->planet.object = lv_image_create(renderer->arena);
    lv_image_set_src(renderer->planet.object, s_planet_images[renderer->planet.image_index]);
    renderer->planet.x = rand() % 720;
    renderer->planet.y = (rand() % 420) * 100;
    renderer->planet.speed = 2 + rand() % 6;
    lv_obj_set_pos(renderer->planet.object, renderer->planet.x, renderer->planet.y / 100);

    for (uint16_t i = 0U; i < SHOOTER_MAX_PLAYER_BULLETS; ++i) {
        renderer->bullet_markers[i] = lv_image_create(renderer->arena);
        lv_image_set_src(renderer->bullet_markers[i], &img_laser_bullet);
        lv_image_set_scale(renderer->bullet_markers[i], 512);
        lv_obj_set_size(renderer->bullet_markers[i], 6, 22);
        lv_obj_add_flag(renderer->bullet_markers[i], LV_OBJ_FLAG_HIDDEN);
    }

    for (uint16_t i = 0U; i < SHOOTER_MAX_ENEMY_BULLETS; ++i) {
        renderer->enemy_bullet_markers[i] = lv_obj_create(renderer->arena);
        lv_obj_set_size(renderer->enemy_bullet_markers[i], 8, 16);
        lv_obj_set_style_radius(renderer->enemy_bullet_markers[i], 6, 0);
        lv_obj_set_style_bg_color(renderer->enemy_bullet_markers[i], lv_color_hex(0xFF6B6B), 0);
        lv_obj_set_style_border_width(renderer->enemy_bullet_markers[i], 0, 0);
        lv_obj_set_style_shadow_width(renderer->enemy_bullet_markers[i], 8, 0);
        lv_obj_set_style_shadow_color(renderer->enemy_bullet_markers[i], lv_color_hex(0xFFA36C), 0);
        lv_obj_clear_flag(renderer->enemy_bullet_markers[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(renderer->enemy_bullet_markers[i], LV_OBJ_FLAG_HIDDEN);
    }

    for (uint16_t i = 0U; i < SHOOTER_MAX_ENEMIES; ++i) {
        renderer->enemy_markers[i] = lv_image_create(renderer->arena);
        lv_image_set_src(renderer->enemy_markers[i], &img_enemy_ship);
        lv_image_set_scale(renderer->enemy_markers[i], 512);
        lv_obj_set_size(renderer->enemy_markers[i], 32, 32);
        lv_obj_add_flag(renderer->enemy_markers[i], LV_OBJ_FLAG_HIDDEN);

        renderer->enemy_warning_lines[i] = lv_obj_create(renderer->arena);
        lv_obj_set_size(renderer->enemy_warning_lines[i], 2, 420);
        lv_obj_set_style_bg_color(renderer->enemy_warning_lines[i], lv_color_hex(0xFF3838), 0);
        lv_obj_set_style_bg_opa(renderer->enemy_warning_lines[i], LV_OPA_30, 0);
        lv_obj_set_style_border_width(renderer->enemy_warning_lines[i], 0, 0);
        lv_obj_add_flag(renderer->enemy_warning_lines[i], LV_OBJ_FLAG_HIDDEN);
    }

    for (uint16_t i = 0U; i < SHOOTER_MAX_EXPLOSIONS; ++i) {
        renderer->explosion_markers[i] = lv_obj_create(renderer->arena);
        lv_obj_set_size(renderer->explosion_markers[i], 16, 16);
        lv_obj_set_style_radius(renderer->explosion_markers[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(renderer->explosion_markers[i], lv_color_hex(0xFFD32D), 0);
        lv_obj_set_style_border_width(renderer->explosion_markers[i], 0, 0);
        lv_obj_set_style_shadow_width(renderer->explosion_markers[i], 12, 0);
        lv_obj_set_style_shadow_color(renderer->explosion_markers[i], lv_color_hex(0xFF7F50), 0);
        lv_obj_add_flag(renderer->explosion_markers[i], LV_OBJ_FLAG_HIDDEN);
    }

    renderer->player_marker = lv_image_create(renderer->arena);
    lv_image_set_src(renderer->player_marker, &img_player_ship);
    lv_image_set_scale(renderer->player_marker, 256);
    lv_obj_set_size(renderer->player_marker, 48, 48);

    renderer->shield_aura = lv_obj_create(renderer->arena);
    lv_obj_set_size(renderer->shield_aura, 160, 160);
    lv_obj_set_style_radius(renderer->shield_aura, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(renderer->shield_aura, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(renderer->shield_aura, 3, 0);
    lv_obj_set_style_border_color(renderer->shield_aura, lv_color_hex(0x00D2D3), 0);
    lv_obj_set_style_shadow_width(renderer->shield_aura, 16, 0);
    lv_obj_set_style_shadow_color(renderer->shield_aura, lv_color_hex(0x00D2D3), 0);
    lv_obj_add_flag(renderer->shield_aura, LV_OBJ_FLAG_HIDDEN);

    renderer->beam_overlay = lv_obj_create(renderer->arena);
    lv_obj_set_style_bg_color(renderer->beam_overlay, lv_color_hex(0x00D2D3), 0);
    lv_obj_set_style_bg_opa(renderer->beam_overlay, LV_OPA_40, 0);
    lv_obj_set_style_border_width(renderer->beam_overlay, 0, 0);
    lv_obj_add_flag(renderer->beam_overlay, LV_OBJ_FLAG_HIDDEN);
    {
        lv_obj_t *beam_core = lv_obj_create(renderer->beam_overlay);
        lv_obj_set_size(beam_core, 24, LV_PCT(100));
        lv_obj_center(beam_core);
        lv_obj_set_style_bg_color(beam_core, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(beam_core, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(beam_core, 0, 0);
        lv_obj_set_style_shadow_width(beam_core, 24, 0);
        lv_obj_set_style_shadow_color(beam_core, lv_color_hex(0x00D2D3), 0);
    }

    renderer->damage_overlay = lv_obj_create(renderer->arena);
    lv_obj_set_size(renderer->damage_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(renderer->damage_overlay, lv_color_hex(0xFF3B30), 0);
    lv_obj_set_style_bg_opa(renderer->damage_overlay, LV_OPA_30, 0);
    lv_obj_set_style_border_width(renderer->damage_overlay, 0, 0);
    lv_obj_add_flag(renderer->damage_overlay, LV_OBJ_FLAG_HIDDEN);

    renderer->danger_overlay = lv_obj_create(renderer->arena);
    lv_obj_set_size(renderer->danger_overlay, LV_PCT(100), 96);
    lv_obj_align(renderer->danger_overlay, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(renderer->danger_overlay, lv_color_hex(0xFFA502), 0);
    lv_obj_set_style_bg_opa(renderer->danger_overlay, LV_OPA_20, 0);
    lv_obj_set_style_border_width(renderer->danger_overlay, 0, 0);
    lv_obj_add_flag(renderer->danger_overlay, LV_OBJ_FLAG_HIDDEN);

    renderer->device_banner = lv_label_create(renderer->arena);
    lv_obj_align(renderer->device_banner, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_text_color(renderer->device_banner, lv_color_hex(0xFFF200), 0);
    lv_obj_add_flag(renderer->device_banner, LV_OBJ_FLAG_HIDDEN);

    renderer->hud_hp = lv_label_create(renderer->arena);
    style_hud_label(renderer->hud_hp, 120, 10);
    renderer->hud_device = lv_label_create(renderer->arena);
    style_hud_label(renderer->hud_device, 170, 140);
    renderer->hud_wave = lv_label_create(renderer->arena);
    style_hud_label(renderer->hud_wave, 155, 320);
    renderer->hud_phase = lv_label_create(renderer->arena);
    style_hud_label(renderer->hud_phase, 150, 485);

    renderer->upgrade_status = lv_label_create(renderer->arena);
    lv_label_set_long_mode(renderer->upgrade_status, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(renderer->upgrade_status, 230);
    lv_obj_align(renderer->upgrade_status, LV_ALIGN_TOP_RIGHT, -10, 8);
    lv_obj_set_style_text_align(renderer->upgrade_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(renderer->upgrade_status, lv_color_hex(0xFFF200), 0);

    renderer->hint = lv_label_create(renderer->arena);
    lv_label_set_long_mode(renderer->hint, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(renderer->hint, 820);
    lv_obj_align(renderer->hint, LV_ALIGN_BOTTOM_LEFT, 14, -8);
    lv_obj_set_style_text_color(renderer->hint, lv_color_hex(0xDFF9FB), 0);
    lv_obj_set_style_text_opa(renderer->hint, LV_OPA_80, 0);

    renderer->cached_hint[0] = '\0';
    renderer->cached_hp = INT16_MIN;
    renderer->cached_damage = INT16_MIN;
    renderer->cached_max_hp = UINT16_MAX;
    renderer->cached_device_percent = UINT16_MAX;
    renderer->cached_wave_completed = UINT16_MAX;
    renderer->cached_wave_total = UINT16_MAX;
    renderer->cached_phase = UINT8_MAX;
    renderer->cached_projectiles = UINT8_MAX;
    renderer->cached_device_unlocked = UINT8_MAX;
    memset(renderer->rendered_archetypes, UINT8_MAX, sizeof(renderer->rendered_archetypes));
}

void shooter_renderer_lvgl_update(shooter_renderer_lvgl_t *renderer, const shooter_core_t *core)
{
    int32_t player_x;
    int32_t player_y;
    char status[96];
    char hint[128];
    uint16_t completed;
    uint16_t device_percent;

    if (renderer == NULL || core == NULL || renderer->arena == NULL) {
        return;
    }

    boss_status_text(status, sizeof(status), core);
    lv_snprintf(hint, sizeof(hint), "%s  |  %s", shooter_core_segment_label(core), status);
    if (strcmp(hint, renderer->cached_hint) != 0) {
        lv_label_set_text(renderer->hint, hint);
        memcpy(renderer->cached_hint, hint, sizeof(renderer->cached_hint));
        renderer->cached_hint[sizeof(renderer->cached_hint) - 1U] = '\0';
    }

    completed = core->wave_budget_total >= core->wave_budget_remaining
        ? (uint16_t)(core->wave_budget_total - core->wave_budget_remaining)
        : 0U;
    device_percent = core->device_cooldown_base_ms == 0U
        ? 100U
        : (uint16_t)(100U - ((uint32_t)core->device_cooldown_ms * 100U /
                             core->device_cooldown_base_ms));
    if (device_percent > 100U) {
        device_percent = 100U;
    }

    if (renderer->cached_hp != core->player.hp ||
        renderer->cached_max_hp != core->balance.player_max_hp) {
        renderer->cached_hp = core->player.hp;
        renderer->cached_max_hp = core->balance.player_max_hp;
        lv_label_set_text_fmt(renderer->hud_hp, "核心 %d/%u",
                              (int)core->player.hp, (unsigned)core->balance.player_max_hp);
    }
    const uint8_t device_unlocked = core->upgrades.shield_unlocked ? 1U : 0U;
    if (renderer->cached_device_unlocked != device_unlocked ||
        (device_unlocked != 0U && renderer->cached_device_percent != device_percent)) {
        renderer->cached_device_unlocked = device_unlocked;
        renderer->cached_device_percent = device_percent;
        if (!core->upgrades.shield_unlocked) {
            lv_label_set_text(renderer->hud_device, "装置 未解锁");
        } else {
            lv_label_set_text_fmt(renderer->hud_device, "装置 %s %u%%",
                                  core->device_cooldown_ms == 0U ? "就绪" : "充能",
                                  (unsigned)device_percent);
        }
    }
    if (renderer->cached_wave_completed != completed ||
        renderer->cached_wave_total != core->wave_budget_total) {
        renderer->cached_wave_completed = completed;
        renderer->cached_wave_total = core->wave_budget_total;
        lv_label_set_text_fmt(renderer->hud_wave, "波次 %u/%u",
                              (unsigned)completed, (unsigned)core->wave_budget_total);
    }
    if (renderer->cached_phase != (uint8_t)core->phase) {
        renderer->cached_phase = (uint8_t)core->phase;
        lv_label_set_text(renderer->hud_phase, shooter_core_phase_name(core->phase));
    }
    if (renderer->cached_damage != core->upgrades.bullet_damage ||
        renderer->cached_projectiles != core->upgrades.projectile_count) {
        renderer->cached_damage = core->upgrades.bullet_damage;
        renderer->cached_projectiles = core->upgrades.projectile_count;
#if defined(SHOOTER_EMBEDDED)
        lv_label_set_text_fmt(renderer->upgrade_status, "发射  伤害%d  %u连发",
#else
        lv_label_set_text_fmt(renderer->upgrade_status, "J发射  伤害%d  %u连发",
#endif
                              (int)core->upgrades.bullet_damage,
                              (unsigned)core->upgrades.projectile_count);
    }

    set_hidden(renderer->damage_overlay, core->feedback_damage_flash_ms == 0U);
    set_hidden(renderer->danger_overlay, core->feedback_danger_flash_ms == 0U);
    set_hidden(renderer->device_banner, core->feedback_device_flash_ms == 0U);
    if (core->feedback_device_flash_ms > 0U) {
        lv_label_set_text(renderer->device_banner,
                          core->upgrades.beam_unlocked ? "星光护盾 + 贯星能量炮" : "星光护盾启动");
    }

    for (uint8_t i = 0U; i < 24U; ++i) {
        shooter_star_t *star = &renderer->stars[i];
        star->y = (int16_t)(star->y + star->speed);
        if (star->y >= 420) {
            star->y = 0;
            star->x = (int16_t)(rand() % 880);
        }
        lv_obj_set_pos(star->object, star->x, star->y);
    }

    renderer->planet.y += renderer->planet.speed;
    if (renderer->planet.y >= 42000) {
        renderer->planet.image_index = (uint8_t)(rand() % SHOOTER_PLANET_COUNT);
        renderer->planet.y = -16000;
        renderer->planet.x = rand() % 720;
        renderer->planet.speed = 2 + rand() % 6;
        lv_image_set_src(renderer->planet.object, s_planet_images[renderer->planet.image_index]);
    }
    lv_obj_set_pos(renderer->planet.object, renderer->planet.x, renderer->planet.y / 100);

    player_x = (core->player.x * 880) / (int32_t)core->logical_width;
    player_y = (core->player.y * 420) / (int32_t)core->logical_height;
    lv_obj_set_pos(renderer->player_marker, player_x - 24, player_y - 24);
    lv_obj_set_style_image_recolor(renderer->player_marker, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_image_recolor_opa(renderer->player_marker,
                                       core->feedback_damage_flash_ms > 0U ? LV_OPA_50 : LV_OPA_TRANSP, 0);

    if (core->shield_active) {
        const int32_t pulse = 150 + ((int32_t)core->shield_duration_ms % 250) / 12;
        lv_obj_set_size(renderer->shield_aura, pulse, pulse);
        lv_obj_set_pos(renderer->shield_aura, player_x - pulse / 2, player_y - pulse / 2);
    }
    set_hidden(renderer->shield_aura, !core->shield_active);

    if (core->beam_active_ms > 0U) {
        lv_obj_set_size(renderer->beam_overlay, 240, player_y);
        lv_obj_set_pos(renderer->beam_overlay, player_x - 120, 0);
    }
    set_hidden(renderer->beam_overlay, core->beam_active_ms == 0U);

    for (uint16_t i = 0U; i < SHOOTER_MAX_PLAYER_BULLETS; ++i) {
        if (core->bullets[i].active) {
            const int32_t x = (core->bullets[i].x * 880) / (int32_t)core->logical_width;
            const int32_t y = (core->bullets[i].y * 420) / (int32_t)core->logical_height;
            lv_obj_set_pos(renderer->bullet_markers[i], x - 3, y - 11);
        }
        set_hidden(renderer->bullet_markers[i], !core->bullets[i].active);
    }

    for (uint16_t i = 0U; i < SHOOTER_MAX_ENEMY_BULLETS; ++i) {
        const shooter_bullet_t *bullet = &core->enemy_bullets[i];
        if (bullet->active) {
            const int32_t x = (bullet->x * 880) / (int32_t)core->logical_width;
            const int32_t y = (bullet->y * 420) / (int32_t)core->logical_height;
            if ((bullet->flags & SHOOTER_BULLET_FLAG_LASER) != 0U) {
                const int32_t height = y < 420 ? 420 - y : 0;
                lv_obj_set_size(renderer->enemy_bullet_markers[i], 10, height);
                lv_obj_set_pos(renderer->enemy_bullet_markers[i], x - 5, y);
                lv_obj_set_style_bg_color(renderer->enemy_bullet_markers[i], lv_color_hex(0xFF3838), 0);
            } else {
                lv_obj_set_size(renderer->enemy_bullet_markers[i], 8, 16);
                lv_obj_set_pos(renderer->enemy_bullet_markers[i], x - 4, y - 8);
                lv_obj_set_style_bg_color(renderer->enemy_bullet_markers[i], lv_color_hex(0xFF6B6B), 0);
            }
        }
        set_hidden(renderer->enemy_bullet_markers[i], !bullet->active);
    }

    for (uint16_t i = 0U; i < SHOOTER_MAX_ENEMIES; ++i) {
        const shooter_enemy_t *enemy = &core->enemies[i];
        if (enemy->active) {
            const int32_t x = (enemy->x * 880) / (int32_t)core->logical_width;
            const int32_t y = (enemy->y * 420) / (int32_t)core->logical_height;
            int32_t width = 32;
            int32_t height = 32;
            lv_color_t tint = lv_color_hex(0xFFFFFF);

            if (enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_BOSS) {
                if (renderer->rendered_archetypes[i] != enemy->archetype) {
                    lv_image_set_src(renderer->enemy_markers[i], &img_boss_ship);
                    lv_image_set_scale(renderer->enemy_markers[i], 320);
                }
                width = 240;
                height = 140;
                tint = lv_color_hex(0xFF4D4D);
            } else if (enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_ELITE_BRUISER) {
                if (renderer->rendered_archetypes[i] != enemy->archetype) {
                    lv_image_set_src(renderer->enemy_markers[i], &img_elite_bruiser);
                    lv_image_set_scale(renderer->enemy_markers[i], 299);
                }
                width = 84;
                height = 84;
            } else if (enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_LASER_DRONE) {
                if (renderer->rendered_archetypes[i] != enemy->archetype) {
                    lv_image_set_src(renderer->enemy_markers[i], &img_laser_drone);
                    lv_image_set_scale(renderer->enemy_markers[i], 299);
                }
                width = 56;
                height = 56;
            } else {
                if (renderer->rendered_archetypes[i] != enemy->archetype) {
                    lv_image_set_src(renderer->enemy_markers[i], &img_enemy_ship);
                    lv_image_set_scale(renderer->enemy_markers[i], 512);
                }
                if (enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_ELITE_DASHER) {
                    width = 36;
                    height = 36;
                    tint = lv_color_hex(0xFFA502);
                } else if (enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_ELITE_PINNER) {
                    width = 38;
                    height = 38;
                    tint = lv_color_hex(0x7BED9F);
                }
            }
            renderer->rendered_archetypes[i] = enemy->archetype;

            lv_obj_set_size(renderer->enemy_markers[i], width, height);
            lv_obj_set_pos(renderer->enemy_markers[i], x - width / 2, y - height / 2);
            lv_obj_set_style_image_recolor(renderer->enemy_markers[i], tint, 0);
            lv_obj_set_style_image_recolor_opa(renderer->enemy_markers[i],
                enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_BOSS
                    ? (core->boss_vulnerable ? LV_OPA_20 : LV_OPA_TRANSP)
                    : ((enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_ELITE_BRUISER ||
                        enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_LASER_DRONE)
                       ? LV_OPA_TRANSP : LV_OPA_30), 0);

            if (enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_LASER_DRONE &&
                enemy->fire_cooldown_ms < 500U && enemy->behavior_state == 1U) {
                const int32_t warning_height = y < 420 ? 420 - y : 0;
                lv_obj_set_size(renderer->enemy_warning_lines[i], 2, warning_height);
                lv_obj_set_pos(renderer->enemy_warning_lines[i], x - 1, y);
                set_hidden(renderer->enemy_warning_lines[i], false);
            } else {
                set_hidden(renderer->enemy_warning_lines[i], true);
            }
        }
        set_hidden(renderer->enemy_markers[i], !enemy->active);
        if (!enemy->active) {
            set_hidden(renderer->enemy_warning_lines[i], true);
        }
    }

    for (uint16_t i = 0U; i < SHOOTER_MAX_EXPLOSIONS; ++i) {
        const shooter_explosion_t *explosion = &core->explosions[i];
        if (explosion->active) {
            const int32_t x = (explosion->x * 880) / (int32_t)core->logical_width;
            const int32_t y = (explosion->y * 420) / (int32_t)core->logical_height;
            const int32_t percent = (explosion->age_ms * 100) / 360;
            const int32_t size = 8 + (percent * 24) / 100;
            lv_obj_set_size(renderer->explosion_markers[i], size, size);
            lv_obj_set_pos(renderer->explosion_markers[i], x - size / 2, y - size / 2);
            lv_obj_set_style_bg_opa(renderer->explosion_markers[i],
                                    percent < 40 ? LV_OPA_COVER : (percent < 85 ? LV_OPA_80 : LV_OPA_30), 0);
        }
        set_hidden(renderer->explosion_markers[i], !explosion->active);
    }
}
