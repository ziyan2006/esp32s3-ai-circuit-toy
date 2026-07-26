#include "shooter_core.h"

#include <stdlib.h>
#include <string.h>

#define SHOOTER_CORE_VERSION 4U
#define SHOOTER_LOGICAL_WIDTH 1024U
#define SHOOTER_LOGICAL_HEIGHT 600U
#define SHOOTER_PLAYER_START_Y (SHOOTER_LOGICAL_HEIGHT - 96U)
#define SHOOTER_BULLET_SPEED 560
#define SHOOTER_ENEMY_SPEED 132
#define SHOOTER_ENEMY_DIVE_SPEED 220
#define SHOOTER_ENEMY_TRACK_SPEED 140
#define SHOOTER_ENEMY_TRACK_EDGE_SPEED 220
#define SHOOTER_ENEMY_BULLET_SPEED 250
#define SHOOTER_TRACKER_REFRESH_MS 140U
#define SHOOTER_TRACKER_EDGE_REFRESH_MS 90U
#define SHOOTER_TRACKER_FIRE_COOLDOWN_MS 900U
#define SHOOTER_TRACKER_EDGE_FIRE_COOLDOWN_MS 650U
#define SHOOTER_SPAWN_COOLDOWN_MS 660U
#define SHOOTER_DEVICE_COOLDOWN_MS 10000U
#define SHOOTER_SHIELD_DURATION_MS 3000U
#define SHOOTER_BEAM_DURATION_MS 1000U
#define SHOOTER_BEAM_HALF_WIDTH 120
#define SHOOTER_PLAYER_RADIUS 24
#define SHOOTER_BULLET_RADIUS 8
#define SHOOTER_ENEMY_RADIUS 22
#define SHOOTER_ENEMY_BULLET_RADIUS 10
#define SHOOTER_EDGE_DANGER_MARGIN 180
#define SHOOTER_DEFAULT_WAVE_BUDGET 16U
#define SHOOTER_HAZARD_TELEGRAPH_MS 520U
#define SHOOTER_HAZARD_DAMAGE_TICK_MS 220U
#define SHOOTER_FEEDBACK_DAMAGE_MS 180U
#define SHOOTER_FEEDBACK_KILL_MS 120U
#define SHOOTER_FEEDBACK_DANGER_MS 140U
#define SHOOTER_FEEDBACK_DEVICE_MS 220U
#define SHOOTER_ENEMY_HP_MULTIPLIER 3

static int32_t shooter_clamp_i32(int32_t value, int32_t min_value, int32_t max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static uint16_t shooter_reduce_cooldown(uint16_t cooldown_ms, uint32_t dt_ms)
{
    if (dt_ms >= cooldown_ms) {
        return 0U;
    }
    return (uint16_t)(cooldown_ms - dt_ms);
}

static uint16_t shooter_compute_stage_spawn_budget(const shooter_wave_plan_t *plan);

static int16_t shooter_scale_enemy_hp(int16_t base_hp)
{
    return (int16_t)(base_hp * SHOOTER_ENEMY_HP_MULTIPLIER);
}

static shooter_bullet_t *shooter_find_free_bullet(shooter_core_t *core)
{
    uint16_t i;
    for (i = 0; i < SHOOTER_MAX_PLAYER_BULLETS; ++i) {
        if (!core->bullets[i].active) {
            return &core->bullets[i];
        }
    }
    return NULL;
}

static shooter_bullet_t *shooter_find_free_enemy_bullet(shooter_core_t *core)
{
    uint16_t i;
    for (i = 0; i < SHOOTER_MAX_ENEMY_BULLETS; ++i) {
        if (!core->enemy_bullets[i].active) {
            return &core->enemy_bullets[i];
        }
    }
    return NULL;
}

static shooter_enemy_t *shooter_find_free_enemy(shooter_core_t *core)
{
    uint16_t i;
    for (i = 0; i < SHOOTER_MAX_ENEMIES; ++i) {
        if (!core->enemies[i].active) {
            return &core->enemies[i];
        }
    }
    return NULL;
}

static bool shooter_overlap(int32_t ax, int32_t ay, int32_t ar, int32_t bx, int32_t by, int32_t br)
{
    const int32_t dx = ax - bx;
    const int32_t dy = ay - by;
    const int32_t sum = ar + br;
    return (dx * dx) + (dy * dy) <= (sum * sum);
}

static int32_t shooter_abs_i32(int32_t value)
{
    return (value < 0) ? -value : value;
}

static void shooter_balance_apply_level(shooter_core_t *core)
{
    const shooter_stage_config_t *stage = shooter_stage_get(core->level);
    uint16_t level_index = (uint16_t)stage->level_id;

    core->upgrades = stage->upgrades;
    core->balance.fire_cooldown_ms = core->upgrades.fire_interval_ms;
    core->balance.spawn_cooldown_ms = SHOOTER_SPAWN_COOLDOWN_MS;
    core->balance.device_cooldown_ms = SHOOTER_DEVICE_COOLDOWN_MS;
    core->balance.bullet_speed = SHOOTER_BULLET_SPEED;
    core->balance.enemy_speed = SHOOTER_ENEMY_SPEED;
    core->balance.wave_budget = SHOOTER_DEFAULT_WAVE_BUDGET;
    core->balance.player_max_hp = core->upgrades.core_count;

    {
        static const uint16_t spawn_ms_by_level[] = {0U, 820U, 720U, 650U, 600U, 560U, 520U};
        static const int16_t enemy_speed_by_level[] = {0, 104, 116, 128, 140, 148, 156};

        core->balance.spawn_cooldown_ms = spawn_ms_by_level[level_index];
        core->balance.enemy_speed = enemy_speed_by_level[level_index];
        core->balance.wave_budget = shooter_compute_stage_spawn_budget(shooter_wave_get_plan(core->level));
    }
}

static int16_t shooter_bullet_speed(const shooter_core_t *core)
{
    return (core != NULL && core->balance.bullet_speed != 0)
        ? core->balance.bullet_speed
        : SHOOTER_BULLET_SPEED;
}

static shooter_bullet_t *shooter_spawn_bullet(shooter_core_t *core,
                                              int32_t x,
                                              int32_t y,
                                              int16_t vx,
                                              int16_t vy,
                                              int16_t damage,
                                              uint8_t ttl_ticks,
                                              uint8_t flags)
{
    shooter_bullet_t *bullet;

    if (core == NULL) {
        return NULL;
    }

    bullet = shooter_find_free_bullet(core);
    if (bullet == NULL) {
        return NULL;
    }

    memset(bullet, 0, sizeof(*bullet));
    bullet->active = true;
    bullet->x = x;
    bullet->y = y;
    bullet->vx = vx;
    bullet->vy = vy;
    bullet->damage = (damage > 0) ? damage : 1;
    bullet->ttl_ticks = ttl_ticks;
    bullet->flags = flags;
    return bullet;
}

static void shooter_spawn_enemy_bullet(shooter_core_t *core, int32_t x, int32_t y, int16_t vx, int16_t vy)
{
    shooter_bullet_t *bullet = shooter_find_free_enemy_bullet(core);

    if (bullet == NULL) {
        return;
    }

    memset(bullet, 0, sizeof(*bullet));
    bullet->active = true;
    bullet->x = x;
    bullet->y = y;
    bullet->vx = vx;
    bullet->vy = vy;
    bullet->damage = 1;
}

static bool shooter_player_is_near_edge(const shooter_core_t *core)
{
    return core != NULL &&
           (core->player.x < SHOOTER_EDGE_DANGER_MARGIN ||
            core->player.x > (int32_t)core->logical_width - SHOOTER_EDGE_DANGER_MARGIN);
}

static void shooter_apply_damage(shooter_core_t *core, uint16_t amount)
{
    if (core == NULL || amount == 0U || core->player.hp <= 0) {
        return;
    }
    if (core->shield_active) {
        return;
    }

    if ((uint16_t)core->player.hp <= amount) {
        core->player.hp = 0;
    } else {
        core->player.hp = (int16_t)(core->player.hp - (int16_t)amount);
    }
    core->total_hits_taken = (uint16_t)(core->total_hits_taken + amount);
    core->feedback_damage_flash_ms = SHOOTER_FEEDBACK_DAMAGE_MS;
}

static void shooter_spawn_explosion(shooter_core_t *core, int32_t x, int32_t y)
{
    uint16_t i;
    core->feedback_kill_flash_ms = SHOOTER_FEEDBACK_KILL_MS;
    for (i = 0; i < SHOOTER_MAX_EXPLOSIONS; ++i) {
        if (!core->explosions[i].active) {
            core->explosions[i].active = true;
            core->explosions[i].x = x;
            core->explosions[i].y = y;
            core->explosions[i].age_ms = 0U;
            return;
        }
    }
}

static bool shooter_finish_enemy_if_dead(shooter_core_t *core, shooter_enemy_t *enemy)
{
    if (core == NULL || enemy == NULL || enemy->hp > 0) {
        return false;
    }

    enemy->active = false;
    core->total_destroyed += 1U;
    if (enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_BOSS) {
        core->boss_defeated = true;
    }
    shooter_spawn_explosion(core, enemy->x, enemy->y);
    return true;
}

static void shooter_spawn_player_volley(shooter_core_t *core)
{
    const uint8_t projectile_count = core->upgrades.projectile_count;
    const int16_t speed = shooter_bullet_speed(core);
    const int16_t damage = core->upgrades.bullet_damage;

    for (uint8_t index = 0U; index < projectile_count; ++index) {
        const int32_t doubled_offset = ((int32_t)index * 2) - ((int32_t)projectile_count - 1);
        const int32_t x = core->player.x + doubled_offset * 12;
        (void)shooter_spawn_bullet(core, x, core->player.y - 18, 0, -speed, damage, 0U, 0U);
    }
}

static void shooter_configure_enemy_stats(shooter_enemy_t *enemy,
                                          shooter_enemy_archetype_t archetype,
                                          const shooter_boss_definition_t *boss_definition,
                                          uint32_t level_id)
{
    static const int16_t speed_delta_by_level[] = {-18, -22, -12, 0, 10, 18, 26};
    uint32_t clamped_level = (level_id > 6U) ? 6U : level_id;
    int16_t speed_delta = speed_delta_by_level[clamped_level];

    enemy->archetype = (uint8_t)archetype;
    enemy->contact_damage = 1U;
    enemy->type = SHOOTER_ENEMY_TYPE_STRAIGHT;
    enemy->behavior_state = 0U;
    enemy->fire_cooldown_ms = 720U;
    enemy->behavior_timer_ms = 0U;

    switch (archetype) {
    case SHOOTER_ENEMY_ARCHETYPE_ELITE_DASHER:
        enemy->type = SHOOTER_ENEMY_TYPE_DIVER;
        enemy->hp = (level_id <= 2U) ? 2 : 3;
        enemy->max_hp = (uint16_t)enemy->hp;
        enemy->radius = 24U;
        enemy->vy = (int16_t)(190 + speed_delta);
        enemy->vx = (rand() % 2 == 0) ? -150 : 150;
        enemy->behavior_timer_ms = (level_id <= 2U) ? 760U : 520U;
        enemy->fire_cooldown_ms = (level_id <= 2U) ? 420U : 0U;
        break;
    case SHOOTER_ENEMY_ARCHETYPE_ELITE_BRUISER:
        enemy->type = SHOOTER_ENEMY_TYPE_TRACKER;
        enemy->hp = (level_id <= 3U) ? 4 : 7;
        enemy->max_hp = (uint16_t)enemy->hp;
        enemy->radius = 30U;
        enemy->vy = (int16_t)(90 + speed_delta);
        enemy->vx = (rand() % 2 == 0) ? -40 : 40;
        enemy->fire_cooldown_ms = (level_id <= 3U) ? 880U : 540U;
        enemy->behavior_timer_ms = (level_id <= 3U) ? 180U : 120U;
        break;
    case SHOOTER_ENEMY_ARCHETYPE_ELITE_PINNER:
        enemy->type = SHOOTER_ENEMY_TYPE_TRACKER;
        enemy->hp = (level_id <= 4U) ? 3 : 4;
        enemy->max_hp = (uint16_t)enemy->hp;
        enemy->radius = 26U;
        enemy->vy = (int16_t)(110 + speed_delta);
        enemy->vx = (rand() % 2 == 0) ? -90 : 90;
        enemy->fire_cooldown_ms = (level_id <= 4U) ? 720U : 480U;
        enemy->behavior_timer_ms = (level_id <= 4U) ? 130U : 90U;
        break;
    case SHOOTER_ENEMY_ARCHETYPE_LASER_DRONE:
        enemy->type = SHOOTER_ENEMY_TYPE_LASER;
        enemy->hp = 2;
        enemy->max_hp = 2U;
        enemy->radius = 26U;
        enemy->vy = (int16_t)(80 + speed_delta / 2);
        enemy->vx = 0;
        enemy->fire_cooldown_ms = 1500U;
        break;
    case SHOOTER_ENEMY_ARCHETYPE_BOSS:
        enemy->type = SHOOTER_ENEMY_TYPE_STRAIGHT;
        enemy->hp = (int16_t)((boss_definition != NULL) ? boss_definition->max_hp : 16U);
        enemy->max_hp = (uint16_t)enemy->hp;
        enemy->radius = 54U;
        enemy->contact_damage = 2U;
        enemy->vy = 20;
        enemy->vx = 140;
        break;
    case SHOOTER_ENEMY_ARCHETYPE_GRUNT:
    default:
        enemy->type = SHOOTER_ENEMY_TYPE_STRAIGHT;
        enemy->hp = 1;
        enemy->radius = 22U;
        enemy->vy = (level_id == 0U) ? (SHOOTER_ENEMY_SPEED + 30) : (int16_t)(118 + speed_delta);
        enemy->vx = 0;
        enemy->fire_cooldown_ms = (level_id <= 1U) ? 1600U :
            ((level_id == 2U) ? 1250U : 820U);
        break;
    }

    enemy->hp = shooter_scale_enemy_hp(enemy->hp);
    enemy->max_hp = (uint16_t)enemy->hp;
}

static void shooter_spawn_enemy_archetype(shooter_core_t *core, shooter_enemy_archetype_t archetype)
{
    const uint16_t slot_count = 8U;
    const uint16_t slot_index = core->total_spawned % slot_count;
    const int32_t lane_width = (int32_t)core->logical_width / (int32_t)(slot_count + 1U);
    shooter_enemy_t *enemy = shooter_find_free_enemy(core);

    if (enemy == NULL) {
        return;
    }

    memset(enemy, 0, sizeof(*enemy));
    enemy->active = true;

    if (archetype == SHOOTER_ENEMY_ARCHETYPE_BOSS) {
        enemy->x = (int32_t)core->logical_width / 2;
        enemy->y = 110;
    } else {
        enemy->x = lane_width * (int32_t)(slot_index + 1U);
        enemy->y = 40;
    }

    shooter_configure_enemy_stats(enemy, archetype, core->boss_definition, core->level);
    core->total_spawned += 1U;
}

static void shooter_enemy_fire_laser(shooter_core_t *core, shooter_enemy_t *enemy)
{
    shooter_bullet_t *bullet = shooter_find_free_enemy_bullet(core);
    if (bullet == NULL) {
        return;
    }
    memset(bullet, 0, sizeof(*bullet));
    bullet->active = true;
    bullet->x = enemy->x;
    bullet->y = enemy->y + 18;
    bullet->vx = 0;
    bullet->vy = 1200;
    bullet->damage = 1;
    bullet->flags = SHOOTER_BULLET_FLAG_LASER;
}

static void shooter_enemy_fire_straight(shooter_core_t *core, shooter_enemy_t *enemy)
{
    shooter_spawn_enemy_bullet(core, enemy->x, enemy->y + 18, 0, SHOOTER_ENEMY_BULLET_SPEED);
}

static void shooter_enemy_fire_fan(shooter_core_t *core, shooter_enemy_t *enemy)
{
    int16_t center_vx = 0;

    if (enemy->vx != 0) {
        center_vx = (int16_t)(enemy->vx / 2);
    }

    shooter_spawn_enemy_bullet(core, enemy->x, enemy->y + 16, center_vx - 90, SHOOTER_ENEMY_BULLET_SPEED - 20);
    shooter_spawn_enemy_bullet(core, enemy->x, enemy->y + 18, center_vx, SHOOTER_ENEMY_BULLET_SPEED + 20);
    shooter_spawn_enemy_bullet(core, enemy->x, enemy->y + 16, center_vx + 90, SHOOTER_ENEMY_BULLET_SPEED - 20);
}

static void shooter_enemy_fire_aimed(shooter_core_t *core, shooter_enemy_t *enemy)
{
    int32_t dx = core->player.x - enemy->x;
    int32_t dy = core->player.y - enemy->y;
    int32_t adx;
    int32_t ady;
    int16_t vx;
    int16_t vy;

    if (dy <= 0) {
        dy = 1;
    }

    adx = dx < 0 ? -dx : dx;
    ady = dy < 0 ? -dy : dy;
    if (adx + ady == 0) {
        vx = 0;
        vy = SHOOTER_ENEMY_BULLET_SPEED;
    } else {
        vx = (int16_t)((dx * SHOOTER_ENEMY_BULLET_SPEED) / (adx + ady));
        vy = (int16_t)((dy * SHOOTER_ENEMY_BULLET_SPEED) / (adx + ady));
        if (vy < 120) {
            vy = 120;
        }
    }

    shooter_spawn_enemy_bullet(core, enemy->x, enemy->y + 18, vx, vy);
}

static void shooter_enemy_fire_intercept(shooter_core_t *core, shooter_enemy_t *enemy, int32_t target_x)
{
    int32_t dx = target_x - enemy->x;
    int32_t dy = core->player.y - enemy->y;
    int32_t adx;
    int32_t ady;
    int16_t vx;
    int16_t vy;

    if (dy <= 0) {
        dy = 1;
    }

    adx = dx < 0 ? -dx : dx;
    ady = dy < 0 ? -dy : dy;
    if (adx + ady == 0) {
        vx = 0;
        vy = SHOOTER_ENEMY_BULLET_SPEED;
    } else {
        vx = (int16_t)((dx * SHOOTER_ENEMY_BULLET_SPEED) / (adx + ady));
        vy = (int16_t)((dy * SHOOTER_ENEMY_BULLET_SPEED) / (adx + ady));
        if (vy < 120) {
            vy = 120;
        }
    }

    shooter_spawn_enemy_bullet(core, enemy->x, enemy->y + 18, vx, vy);
}

static void shooter_boss_fire_wide_fan(shooter_core_t *core, shooter_enemy_t *enemy, uint8_t intensity)
{
    int16_t center_vx = (int16_t)shooter_clamp_i32((core->player.x - enemy->x) / 6, -70, 70);
    int16_t step = (int16_t)(92 - (intensity * 10));
    int16_t count = (int16_t)(5 + ((intensity > 0U) ? ((intensity - 1U) * 2) : 0U));
    int16_t middle = (int16_t)(count / 2);
    int16_t i;

    for (i = 0; i < count; ++i) {
        int16_t offset = (int16_t)(i - middle);
        int16_t vx = (int16_t)(center_vx + (offset * step));
        int16_t vy = (int16_t)(SHOOTER_ENEMY_BULLET_SPEED + 45 - (shooter_abs_i32(offset) * 10));
        shooter_spawn_enemy_bullet(core, enemy->x, enemy->y + 18, vx, vy);
    }
}

static void shooter_boss_fire_cross_burst(shooter_core_t *core, shooter_enemy_t *enemy, uint8_t intensity)
{
    uint8_t layers = (uint8_t)(intensity + 1U);
    uint8_t layer;

    for (layer = 0U; layer < layers; ++layer) {
        int16_t vx = (int16_t)(120 + ((int16_t)layer * 55));
        int16_t vy = (int16_t)(210 + ((int16_t)layer * 30));
        shooter_spawn_enemy_bullet(core, enemy->x - 26, enemy->y + 10, vx, vy);
        shooter_spawn_enemy_bullet(core, enemy->x + 26, enemy->y + 10, (int16_t)-vx, vy);
        shooter_spawn_enemy_bullet(core, enemy->x - 10, enemy->y + 18, (int16_t)(vx / 2), (int16_t)(vy + 20));
        shooter_spawn_enemy_bullet(core, enemy->x + 10, enemy->y + 18, (int16_t)(-vx / 2), (int16_t)(vy + 20));
    }
}

static void shooter_boss_fire_spiral_sweep(shooter_core_t *core, shooter_enemy_t *enemy, uint8_t intensity, uint8_t cycle)
{
    static const int16_t s_sweep_vx[] = {-220, -150, -70, 10, 90, 170, 240, 120};
    int16_t base = s_sweep_vx[cycle % (uint8_t)(sizeof(s_sweep_vx) / sizeof(s_sweep_vx[0]))];
    int16_t count = (int16_t)(3 + intensity);
    int16_t step = (int16_t)(70 - ((int16_t)intensity * 10));
    int16_t middle = (int16_t)(count / 2);
    int16_t i;

    for (i = 0; i < count; ++i) {
        int16_t offset = (int16_t)(i - middle);
        int16_t vx = (int16_t)(base + (offset * step));
        int16_t vy = (int16_t)(SHOOTER_ENEMY_BULLET_SPEED + 55 - (shooter_abs_i32(offset) * 12));
        shooter_spawn_enemy_bullet(core, enemy->x, enemy->y + 16, vx, vy);
    }
}

static void shooter_boss_fire_gate_volley(shooter_core_t *core, shooter_enemy_t *enemy, uint8_t intensity, uint8_t cycle)
{
    static const int16_t s_lane_offsets[] = {-240, -180, -120, -60, 0, 60, 120, 180, 240};
    int32_t gap_center = core->player.x;
    int32_t gap_half_width = 110 - ((int32_t)intensity * 12);
    uint8_t i;

    if ((cycle % 2U) == 1U) {
        gap_center += (core->player.x < (int32_t)core->logical_width / 2) ? 72 : -72;
    }
    gap_center = shooter_clamp_i32(gap_center, 120, (int32_t)core->logical_width - 120);

    for (i = 0U; i < (uint8_t)(sizeof(s_lane_offsets) / sizeof(s_lane_offsets[0])); ++i) {
        int32_t origin_x = shooter_clamp_i32(enemy->x + s_lane_offsets[i], 56, (int32_t)core->logical_width - 56);
        if (shooter_abs_i32(origin_x - gap_center) <= gap_half_width) {
            continue;
        }
        shooter_spawn_enemy_bullet(core, origin_x, enemy->y + 18, 0, (int16_t)(SHOOTER_ENEMY_BULLET_SPEED + 35));
    }

    if (intensity >= 3U) {
        shooter_spawn_enemy_bullet(core, gap_center - (int32_t)gap_half_width - 18, enemy->y + 12, -40, SHOOTER_ENEMY_BULLET_SPEED);
        shooter_spawn_enemy_bullet(core, gap_center + (int32_t)gap_half_width + 18, enemy->y + 12, 40, SHOOTER_ENEMY_BULLET_SPEED);
    }
}

static shooter_boss_attack_style_t shooter_boss_attack_style_for_cycle(const shooter_boss_phase_t *phase, uint8_t cycle)
{
    if (phase->secondary_attack_style != SHOOTER_BOSS_ATTACK_NONE && (cycle % 2U) == 1U) {
        return phase->secondary_attack_style;
    }

    return phase->primary_attack_style;
}

static void shooter_boss_fire_attack_pattern(shooter_core_t *core,
                                             shooter_enemy_t *enemy,
                                             const shooter_boss_phase_t *phase)
{
    shooter_boss_attack_style_t style = shooter_boss_attack_style_for_cycle(phase, core->boss_attack_cycle);

    switch (style) {
    case SHOOTER_BOSS_ATTACK_WIDE_FAN:
        shooter_boss_fire_wide_fan(core, enemy, phase->attack_intensity);
        break;
    case SHOOTER_BOSS_ATTACK_CROSS_BURST:
        shooter_boss_fire_cross_burst(core, enemy, phase->attack_intensity);
        break;
    case SHOOTER_BOSS_ATTACK_SPIRAL_SWEEP:
        shooter_boss_fire_spiral_sweep(core, enemy, phase->attack_intensity, core->boss_attack_cycle);
        break;
    case SHOOTER_BOSS_ATTACK_GATE_VOLLEY:
        shooter_boss_fire_gate_volley(core, enemy, phase->attack_intensity, core->boss_attack_cycle);
        break;
    case SHOOTER_BOSS_ATTACK_NONE:
    default:
        break;
    }

    core->boss_attack_cycle += 1U;
}

static shooter_enemy_t *shooter_find_active_boss(shooter_core_t *core)
{
    uint16_t i;
    for (i = 0; i < SHOOTER_MAX_ENEMIES; ++i) {
        if (core->enemies[i].active &&
            core->enemies[i].archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_BOSS) {
            return &core->enemies[i];
        }
    }
    return NULL;
}

static uint16_t shooter_count_active_non_boss_enemies(const shooter_core_t *core)
{
    uint16_t count = 0U;
    uint16_t i;
    for (i = 0; i < SHOOTER_MAX_ENEMIES; ++i) {
        if (core->enemies[i].active &&
            core->enemies[i].archetype != (uint8_t)SHOOTER_ENEMY_ARCHETYPE_BOSS) {
            count += 1U;
        }
    }
    return count;
}

static uint16_t shooter_compute_stage_spawn_budget(const shooter_wave_plan_t *plan)
{
    uint8_t i;
    uint16_t total = 0U;
    if (plan == NULL) {
        return SHOOTER_DEFAULT_WAVE_BUDGET;
    }
    for (i = 0U; i < plan->segment_count; ++i) {
        total = (uint16_t)(total + plan->segments[i].spawn_count);
    }
    return total;
}

static bool shooter_segment_duration_expired(const shooter_wave_segment_t *segment,
                                             uint16_t segment_elapsed_ms)
{
    return segment != NULL && segment->duration_ms != 0U && segment_elapsed_ms >= segment->duration_ms;
}

static bool shooter_segment_spawn_budget_met(const shooter_wave_segment_t *segment,
                                             uint16_t segment_spawned)
{
    return segment != NULL && segment_spawned >= segment->spawn_count;
}

static bool shooter_segment_clear_gate_met(const shooter_wave_segment_t *segment,
                                           const shooter_core_t *core)
{
    uint16_t active_count;

    if (segment == NULL || core == NULL) {
        return false;
    }

    active_count = shooter_count_active_non_boss_enemies(core);
    if (!segment->wait_for_clear) {
        return active_count <= 2U;
    }

    return active_count == 0U;
}

static bool shooter_segment_ready_to_advance(const shooter_wave_segment_t *segment,
                                             const shooter_core_t *core)
{
    bool duration_expired;
    bool spawn_budget_met;
    bool clear_gate_met;

    if (segment == NULL || core == NULL) {
        return false;
    }

    duration_expired = shooter_segment_duration_expired(segment, core->segment_elapsed_ms);
    spawn_budget_met = shooter_segment_spawn_budget_met(segment, core->segment_spawned);
    clear_gate_met = shooter_segment_clear_gate_met(segment, core);

    if (segment->tier == SHOOTER_WAVE_TIER_BREATHER) {
        return duration_expired && clear_gate_met;
    }

    if (segment->wait_for_clear) {
        return (duration_expired || spawn_budget_met) && clear_gate_met;
    }

    return duration_expired || (spawn_budget_met && clear_gate_met);
}

static bool shooter_segment_can_spawn_more(const shooter_wave_segment_t *segment,
                                           const shooter_core_t *core)
{
    if (segment == NULL || core == NULL) {
        return false;
    }

    if (segment->spawn_count != 0U && core->segment_spawned >= segment->spawn_count) {
        return false;
    }

    if (shooter_segment_duration_expired(segment, core->segment_elapsed_ms) &&
        segment->wait_for_clear) {
        return false;
    }

    return true;
}

static void shooter_reset_boss_phase_state(shooter_core_t *core, const shooter_boss_phase_t *phase)
{
    if (core == NULL || phase == NULL) {
        return;
    }

    core->boss_support_cooldown_ms = phase->support_spawn_interval_ms;
    core->boss_attack_cooldown_ms = phase->attack_interval_ms;
    core->boss_attack_duration_ms = 0U;
    core->boss_attack_volley_cooldown_ms = 0U;
    core->boss_recovery_ms = phase->recovery_window_ms;
    core->boss_vulnerability_cycle_ms = 0U;
    core->boss_hazard_telegraph_ms = 0U;
    core->boss_hazard_active_ms = 0U;
    core->boss_support_burst_remaining = phase->support_burst_count;
    core->boss_attack_variant = 0U;
    core->boss_attack_cycle = 0U;
    core->boss_hazard_kind = phase->hazard_kind;
    core->boss_attack_active = false;
    core->boss_vulnerable = false;
}

static uint16_t shooter_boss_hazard_duration_ms(shooter_hazard_kind_t kind)
{
    switch (kind) {
    case SHOOTER_HAZARD_TARGET_COLUMN:
        return 900U;
    case SHOOTER_HAZARD_EDGE_WALLS:
        return 1100U;
    case SHOOTER_HAZARD_BOTTOM_SWEEP:
        return 950U;
    case SHOOTER_HAZARD_GATE_PAIR:
        return 1200U;
    case SHOOTER_HAZARD_CENTER_WALL:
        return 1000U;
    case SHOOTER_HAZARD_NONE:
    default:
        return 0U;
    }
}

static void shooter_boss_capture_hazard_anchor(shooter_core_t *core)
{
    if (core == NULL || core->logical_width == 0U) {
        return;
    }

    core->boss_attack_variant = (uint8_t)((core->player.x * 255) / (int32_t)core->logical_width);
}

static int32_t shooter_boss_hazard_anchor_x(const shooter_core_t *core)
{
    if (core == NULL) {
        return 0;
    }

    return ((int32_t)core->boss_attack_variant * (int32_t)core->logical_width) / 255;
}

static bool shooter_is_player_in_hazard(const shooter_core_t *core)
{
    int32_t anchor_x;
    int32_t center_x;

    if (core == NULL || core->boss_hazard_active_ms == 0U) {
        return false;
    }

    anchor_x = shooter_boss_hazard_anchor_x(core);
    center_x = (int32_t)core->logical_width / 2;

    switch (core->boss_hazard_kind) {
    case SHOOTER_HAZARD_TARGET_COLUMN:
        return shooter_abs_i32(core->player.x - anchor_x) <= 82;
    case SHOOTER_HAZARD_EDGE_WALLS:
        return core->player.x <= 160 || core->player.x >= (int32_t)core->logical_width - 160;
    case SHOOTER_HAZARD_BOTTOM_SWEEP:
        return core->player.y >= (int32_t)core->logical_height - 170;
    case SHOOTER_HAZARD_GATE_PAIR:
        return shooter_abs_i32(core->player.x - anchor_x) >= 120;
    case SHOOTER_HAZARD_CENTER_WALL:
        return shooter_abs_i32(core->player.x - center_x) <= 132;
    case SHOOTER_HAZARD_NONE:
    default:
        return false;
    }
}

static void shooter_push_player_from_hazard(shooter_core_t *core)
{
    int32_t next_x = core->player.x;
    int32_t next_y = core->player.y;
    int32_t anchor_x = shooter_boss_hazard_anchor_x(core);
    int32_t center_x = (int32_t)core->logical_width / 2;

    switch (core->boss_hazard_kind) {
    case SHOOTER_HAZARD_TARGET_COLUMN:
        next_x += (core->player.x < anchor_x) ? -54 : 54;
        break;
    case SHOOTER_HAZARD_EDGE_WALLS:
        next_x += (core->player.x < center_x) ? 64 : -64;
        break;
    case SHOOTER_HAZARD_BOTTOM_SWEEP:
        next_y -= 60;
        break;
    case SHOOTER_HAZARD_GATE_PAIR:
        next_x += (core->player.x < anchor_x) ? 48 : -48;
        break;
    case SHOOTER_HAZARD_CENTER_WALL:
        next_x += (core->player.x < center_x) ? -58 : 58;
        break;
    case SHOOTER_HAZARD_NONE:
    default:
        break;
    }

    core->player.x = shooter_clamp_i32(next_x, 48, (int32_t)core->logical_width - 48);
    core->player.y = shooter_clamp_i32(next_y, 120, (int32_t)core->logical_height - 48);
}

static void shooter_apply_boss_hazard(shooter_core_t *core, uint32_t dt_ms)
{
    uint16_t previous_active_ms;

    if (core == NULL || core->boss_hazard_kind == SHOOTER_HAZARD_NONE) {
        return;
    }

    if (core->boss_hazard_telegraph_ms > 0U) {
        core->boss_hazard_telegraph_ms = shooter_reduce_cooldown(core->boss_hazard_telegraph_ms, dt_ms);
        if (core->boss_hazard_telegraph_ms == 0U) {
            core->boss_hazard_active_ms = shooter_boss_hazard_duration_ms(core->boss_hazard_kind);
        }
    }

    if (core->boss_hazard_active_ms == 0U) {
        return;
    }

    previous_active_ms = core->boss_hazard_active_ms;
    core->boss_hazard_active_ms = shooter_reduce_cooldown(core->boss_hazard_active_ms, dt_ms);
    if (!shooter_is_player_in_hazard(core)) {
        return;
    }

    if ((previous_active_ms / SHOOTER_HAZARD_DAMAGE_TICK_MS) !=
        (core->boss_hazard_active_ms / SHOOTER_HAZARD_DAMAGE_TICK_MS)) {
        shooter_apply_damage(core, 1U);
        shooter_push_player_from_hazard(core);
    }
}

void shooter_core_configure_level(shooter_core_t *core, uint32_t level_id)
{
    if (core == NULL) {
        return;
    }

    core->stage_config = shooter_stage_get(level_id);
    core->level = core->stage_config->level_id;
    core->elapsed_ms = 0U;
    core->total_spawned = 0U;
    core->total_destroyed = 0U;
    core->total_hits_taken = 0U;
    core->total_device_activations = 0U;
    core->spawn_cooldown_ms = 1U;
    core->spawn_cooldown_base_ms = SHOOTER_SPAWN_COOLDOWN_MS;
    core->device_cooldown_ms = 0U;
    core->device_cooldown_base_ms = SHOOTER_DEVICE_COOLDOWN_MS;
    core->shield_active = false;
    core->shield_duration_ms = 0U;
    core->beam_active_ms = 0U;
    core->beam_cast_id = 0U;
    core->fire_edge_pressed = false;
    core->fire_held_last_frame = false;
    core->device_edge_pressed = false;
    core->device_held_last_frame = false;
    core->feedback_damage_flash_ms = 0U;
    core->feedback_kill_flash_ms = 0U;
    core->feedback_danger_flash_ms = 0U;
    core->feedback_device_flash_ms = 0U;
    core->spawn_wave_type = SHOOTER_ENEMY_TYPE_STRAIGHT;
    core->spawn_wave_step = 0U;
    core->spawn_wave_remaining = 0U;
    core->spawn_wave_is_edge_pressure = 0U;
    core->spawn_wave_edge_side = 0U;
    core->completed_waves = 0U;
    core->edge_pressure_waves = 0U;
    core->segment_elapsed_ms = 0U;
    core->segment_spawned = 0U;
    core->boss_support_cooldown_ms = 0U;
    core->boss_attack_cooldown_ms = 0U;
    core->boss_attack_duration_ms = 0U;
    core->boss_attack_volley_cooldown_ms = 0U;
    core->boss_recovery_ms = 0U;
    core->boss_vulnerability_cycle_ms = 0U;
    core->boss_hazard_telegraph_ms = 0U;
    core->boss_hazard_active_ms = 0U;
    core->boss_support_burst_remaining = 0U;
    core->segment_index = 0U;
    core->active_boss_phase = 0U;
    core->boss_attack_variant = 0U;
    core->boss_attack_cycle = 0U;
    core->boss_hazard_kind = SHOOTER_HAZARD_NONE;
    core->boss_attack_active = false;
    core->boss_vulnerable = false;
    core->boss_spawned = false;
    core->boss_defeated = false;
    core->stage_script_complete = false;
    core->phase = SHOOTER_PHASE_RUNNING;
    memset(core->bullets, 0, sizeof(core->bullets));
    memset(core->enemy_bullets, 0, sizeof(core->enemy_bullets));
    memset(core->enemies, 0, sizeof(core->enemies));
    memset(core->explosions, 0, sizeof(core->explosions));

    shooter_balance_apply_level(core);
    core->spawn_cooldown_base_ms = core->balance.spawn_cooldown_ms;
    core->device_cooldown_base_ms = core->balance.device_cooldown_ms;
    core->wave_plan = shooter_wave_get_plan(core->level);
    core->boss_definition = shooter_boss_get(core->level);
    core->stage_spawn_budget_total = shooter_compute_stage_spawn_budget(core->wave_plan);
    core->stage_spawn_budget_spent = 0U;
    core->wave_budget_remaining = core->stage_spawn_budget_total;
    core->wave_budget_total = core->stage_spawn_budget_total;
    core->player.x = SHOOTER_LOGICAL_WIDTH / 2U;
    core->player.y = SHOOTER_PLAYER_START_Y;
    core->player.hp = (int16_t)core->balance.player_max_hp;
    core->player.fire_cooldown_ms = 0U;
}

static void shooter_activate_device(shooter_core_t *core)
{
    if (core == NULL || !core->upgrades.shield_unlocked ||
        core->device_cooldown_ms != 0U || !core->device_edge_pressed) {
        return;
    }

    core->total_device_activations += 1U;
    core->feedback_device_flash_ms = SHOOTER_FEEDBACK_DEVICE_MS;
    core->shield_active = true;
    core->shield_duration_ms = SHOOTER_SHIELD_DURATION_MS;
    core->device_cooldown_ms = SHOOTER_DEVICE_COOLDOWN_MS;

    if (core->upgrades.beam_unlocked) {
        core->beam_cast_id += 1U;
        if (core->beam_cast_id == 0U) {
            core->beam_cast_id = 1U;
            for (uint16_t i = 0U; i < SHOOTER_MAX_ENEMIES; ++i) {
                core->enemies[i].last_beam_cast_id = 0U;
            }
        }
        core->beam_active_ms = SHOOTER_BEAM_DURATION_MS;
    }
}

static void shooter_resolve_beam_hits(shooter_core_t *core)
{
    if (core == NULL || core->beam_active_ms == 0U || core->beam_cast_id == 0U) {
        return;
    }

    for (uint16_t i = 0U; i < SHOOTER_MAX_ENEMIES; ++i) {
        shooter_enemy_t *enemy = &core->enemies[i];
        const int32_t dx = shooter_abs_i32(enemy->x - core->player.x);

        if (!enemy->active || enemy->y >= core->player.y ||
            dx > SHOOTER_BEAM_HALF_WIDTH || enemy->last_beam_cast_id == core->beam_cast_id) {
            continue;
        }

        enemy->last_beam_cast_id = core->beam_cast_id;
        enemy->hp -= (int16_t)(core->upgrades.bullet_damage * 3);
        (void)shooter_finish_enemy_if_dead(core, enemy);
    }
}

static void shooter_update_player(shooter_core_t *core,
                                  const shooter_input_state_t *input,
                                  uint32_t dt_ms)
{
    int32_t next_x;
    int32_t next_y;
    int16_t mx = 0;
    int16_t my = 0;
    const int32_t player_speed = core->upgrades.player_speed;

    if (input == NULL) {
        return;
    }

    mx = input->move_x;
    my = input->move_y;

    next_x = core->player.x + ((int32_t)mx * player_speed * (int32_t)dt_ms) / 1000;
    next_y = core->player.y + ((int32_t)my * player_speed * (int32_t)dt_ms) / 1000;
    core->player.x = shooter_clamp_i32(next_x, 48, (int32_t)core->logical_width - 48);
    core->player.y = shooter_clamp_i32(next_y, 120, (int32_t)core->logical_height - 48);
    core->player.fire_cooldown_ms = shooter_reduce_cooldown(core->player.fire_cooldown_ms, dt_ms);
    core->device_cooldown_ms = shooter_reduce_cooldown(core->device_cooldown_ms, dt_ms);

    if (core->fire_edge_pressed && core->player.fire_cooldown_ms == 0U) {
        shooter_spawn_player_volley(core);
        core->player.fire_cooldown_ms = core->upgrades.fire_interval_ms;
    }
}

static void shooter_update_bullets(shooter_core_t *core, uint32_t dt_ms)
{
    uint16_t i;
    for (i = 0U; i < SHOOTER_MAX_PLAYER_BULLETS; ++i) {
        shooter_bullet_t *bullet = &core->bullets[i];
        if (!bullet->active) {
            continue;
        }
        bullet->x += (bullet->vx * (int32_t)dt_ms) / 1000;
        bullet->y += (bullet->vy * (int32_t)dt_ms) / 1000;
        if (bullet->y < -24 || bullet->x < -40 || bullet->x > (int32_t)core->logical_width + 40) {
            bullet->active = false;
        }
    }

    for (i = 0U; i < SHOOTER_MAX_ENEMY_BULLETS; ++i) {
        shooter_bullet_t *bullet = &core->enemy_bullets[i];
        if (!bullet->active) {
            continue;
        }
        bullet->x += (bullet->vx * (int32_t)dt_ms) / 1000;
        bullet->y += (bullet->vy * (int32_t)dt_ms) / 1000;
        if (bullet->y > (int32_t)core->logical_height + 24 ||
            bullet->x < -40 || bullet->x > (int32_t)core->logical_width + 40) {
            bullet->active = false;
        }
    }
}

static void shooter_advance_segment(shooter_core_t *core)
{
    core->segment_index = (uint8_t)(core->segment_index + 1U);
    core->segment_elapsed_ms = 0U;
    core->segment_spawned = 0U;
    core->spawn_cooldown_ms = 1U;
    core->boss_attack_cooldown_ms = 0U;
    core->boss_attack_duration_ms = 0U;
    core->boss_recovery_ms = 0U;
    core->boss_vulnerability_cycle_ms = 0U;
    core->boss_hazard_telegraph_ms = 0U;
    core->boss_hazard_active_ms = 0U;
    core->boss_support_burst_remaining = 0U;
    core->boss_attack_variant = 0U;
    core->boss_hazard_kind = SHOOTER_HAZARD_NONE;
    core->boss_attack_active = false;
    core->boss_vulnerable = false;
    if (core->wave_plan == NULL || core->segment_index >= core->wave_plan->segment_count) {
        core->stage_script_complete = true;
    }
}

static void shooter_update_wave_director(shooter_core_t *core, uint32_t dt_ms)
{
    const shooter_wave_segment_t *segment;

    if (core->wave_plan == NULL || core->stage_script_complete || core->segment_index >= core->wave_plan->segment_count) {
        core->stage_script_complete = true;
        return;
    }

    segment = &core->wave_plan->segments[core->segment_index];
    core->segment_elapsed_ms = (uint16_t)(core->segment_elapsed_ms + dt_ms);

    if (segment->tier == SHOOTER_WAVE_TIER_BREATHER) {
        if (shooter_segment_ready_to_advance(segment, core)) {
            shooter_advance_segment(core);
        }
        return;
    }

    if (segment->tier == SHOOTER_WAVE_TIER_BOSS) {
        if (!core->boss_spawned) {
            shooter_spawn_enemy_archetype(core, SHOOTER_ENEMY_ARCHETYPE_BOSS);
            core->boss_spawned = true;
            core->spawn_cooldown_ms = 0U;
            core->active_boss_phase = 0xFFU;
            if (core->boss_definition != NULL) {
                shooter_reset_boss_phase_state(core, &core->boss_definition->phases[0]);
            }
        }
        if (core->boss_defeated &&
            shooter_find_active_boss(core) == NULL &&
            shooter_count_active_non_boss_enemies(core) == 0U) {
            shooter_advance_segment(core);
        }
        return;
    }

    core->spawn_cooldown_ms = shooter_reduce_cooldown(core->spawn_cooldown_ms, dt_ms);
    if (core->spawn_cooldown_ms == 0U && shooter_segment_can_spawn_more(segment, core)) {
        shooter_enemy_archetype_t archetype = SHOOTER_ENEMY_ARCHETYPE_GRUNT;
        core->segment_spawned = (uint16_t)(core->segment_spawned + 1U);
        core->stage_spawn_budget_spent = (uint16_t)(core->stage_spawn_budget_spent + 1U);
        if (segment->elite_every != 0U && (core->segment_spawned % segment->elite_every) == 0U) {
            archetype = segment->elite_type;
        }
        shooter_spawn_enemy_archetype(core, archetype);
        core->spawn_cooldown_ms = segment->spawn_interval_ms;
        core->wave_budget_remaining = (core->stage_spawn_budget_total > core->stage_spawn_budget_spent)
            ? (uint16_t)(core->stage_spawn_budget_total - core->stage_spawn_budget_spent)
            : 0U;
    }

    if (shooter_segment_ready_to_advance(segment, core)) {
        shooter_advance_segment(core);
    }
}

static void shooter_update_enemies(shooter_core_t *core, uint32_t dt_ms)
{
    uint16_t i;
    shooter_update_wave_director(core, dt_ms);

    for (i = 0U; i < SHOOTER_MAX_ENEMIES; ++i) {
        shooter_enemy_t *enemy = &core->enemies[i];
        if (!enemy->active) {
            continue;
        }

        enemy->fire_cooldown_ms = shooter_reduce_cooldown(enemy->fire_cooldown_ms, dt_ms);
        enemy->behavior_timer_ms = shooter_reduce_cooldown(enemy->behavior_timer_ms, dt_ms);

        if (enemy->archetype != (uint8_t)SHOOTER_ENEMY_ARCHETYPE_BOSS) {
            if (enemy->type == SHOOTER_ENEMY_TYPE_STRAIGHT) {
                if (enemy->fire_cooldown_ms == 0U && enemy->y > 80) {
                    shooter_enemy_fire_straight(core, enemy);
                    if (enemy->behavior_state == 1U && shooter_player_is_near_edge(core)) {
                        if (core->player.x < SHOOTER_EDGE_DANGER_MARGIN) {
                            shooter_enemy_fire_intercept(core, enemy, core->player.x + 150);
                        } else if (core->player.x > (int32_t)core->logical_width - SHOOTER_EDGE_DANGER_MARGIN) {
                            shooter_enemy_fire_intercept(core, enemy, core->player.x - 150);
                        }
                    }
                    enemy->fire_cooldown_ms = (enemy->behavior_state == 1U && shooter_player_is_near_edge(core)) ? 700U : 1200U;
                }
            } else if (enemy->type == SHOOTER_ENEMY_TYPE_DIVER) {
                if (enemy->behavior_state == 0U) {
                    if (enemy->behavior_timer_ms == 0U || enemy->y > 180) {
                        enemy->behavior_state = 1U;
                        enemy->vx = (int16_t)(enemy->vx / 3);
                        enemy->vy = 90;
                        enemy->fire_cooldown_ms = 140U;
                        enemy->behavior_timer_ms = 320U;
                    }
                } else if (enemy->behavior_state == 1U) {
                    if (enemy->fire_cooldown_ms == 0U) {
                        shooter_enemy_fire_fan(core, enemy);
                        enemy->fire_cooldown_ms = 9999U;
                    }
                    if (enemy->behavior_timer_ms == 0U) {
                        enemy->behavior_state = 2U;
                        enemy->vx = (core->player.x >= enemy->x) ? SHOOTER_ENEMY_DIVE_SPEED : -SHOOTER_ENEMY_DIVE_SPEED;
                        enemy->vy = SHOOTER_ENEMY_SPEED + 80;
                        enemy->behavior_timer_ms = 260U;
                        enemy->fire_cooldown_ms = shooter_player_is_near_edge(core) ? 180U : 420U;
                    }
                } else {
                    if (enemy->behavior_timer_ms == 0U) {
                        int32_t player_bias = core->player.x - enemy->x;
                        if (player_bias > 24) {
                            enemy->vx += 45;
                        } else if (player_bias < -24) {
                            enemy->vx -= 45;
                        }
                        enemy->vx = (int16_t)shooter_clamp_i32(enemy->vx,
                                                                -SHOOTER_ENEMY_DIVE_SPEED - 40,
                                                                SHOOTER_ENEMY_DIVE_SPEED + 40);
                        enemy->behavior_timer_ms = 220U;
                    }
                    if (enemy->fire_cooldown_ms == 0U && shooter_player_is_near_edge(core)) {
                        shooter_enemy_fire_fan(core, enemy);
                        enemy->fire_cooldown_ms = 9999U;
                    }
                }
            } else if (enemy->type == SHOOTER_ENEMY_TYPE_TRACKER) {
                if (enemy->behavior_timer_ms == 0U) {
                    int32_t target_x = core->player.x;
                    int32_t dx;
                    int32_t accel = 55;
                    int32_t max_track_speed = SHOOTER_ENEMY_TRACK_SPEED;

                    if (core->player.x < SHOOTER_EDGE_DANGER_MARGIN) {
                        target_x = core->player.x + 96;
                        accel = 90;
                        max_track_speed = SHOOTER_ENEMY_TRACK_EDGE_SPEED;
                    } else if (core->player.x > (int32_t)core->logical_width - SHOOTER_EDGE_DANGER_MARGIN) {
                        target_x = core->player.x - 96;
                        accel = 90;
                        max_track_speed = SHOOTER_ENEMY_TRACK_EDGE_SPEED;
                    }

                    dx = target_x - enemy->x;
                    if (dx > 14) {
                        enemy->vx += (int16_t)accel;
                    } else if (dx < -14) {
                        enemy->vx -= (int16_t)accel;
                    } else {
                        enemy->vx = (int16_t)(enemy->vx / 2);
                    }
                    enemy->vx = (int16_t)shooter_clamp_i32(enemy->vx, -max_track_speed, max_track_speed);
                    enemy->behavior_timer_ms = shooter_player_is_near_edge(core)
                        ? SHOOTER_TRACKER_EDGE_REFRESH_MS
                        : SHOOTER_TRACKER_REFRESH_MS;
                }
                if (enemy->fire_cooldown_ms == 0U && enemy->y > 100) {
                    shooter_enemy_fire_aimed(core, enemy);
                    if (core->player.x < SHOOTER_EDGE_DANGER_MARGIN) {
                        shooter_enemy_fire_intercept(core, enemy, core->player.x + 180);
                    } else if (core->player.x > (int32_t)core->logical_width - SHOOTER_EDGE_DANGER_MARGIN) {
                        shooter_enemy_fire_intercept(core, enemy, core->player.x - 180);
                    }
                    enemy->fire_cooldown_ms = shooter_player_is_near_edge(core)
                        ? SHOOTER_TRACKER_EDGE_FIRE_COOLDOWN_MS
                        : SHOOTER_TRACKER_FIRE_COOLDOWN_MS;
                }
            } else if (enemy->type == SHOOTER_ENEMY_TYPE_LASER) {
                if (enemy->behavior_state == 0U) {
                    if (enemy->y >= 140) {
                        enemy->behavior_state = 1U;
                        enemy->vy = 0;
                        enemy->fire_cooldown_ms = 1200U;
                    }
                } else if (enemy->behavior_state == 1U) {
                    if (enemy->fire_cooldown_ms < 500U) {
                        enemy->vx = 0;
                    } else {
                        int32_t dx = core->player.x - enemy->x;
                        if (dx > 8) {
                            enemy->vx = 60;
                        } else if (dx < -8) {
                            enemy->vx = -60;
                        } else {
                            enemy->vx = 0;
                        }
                    }
                    if (enemy->fire_cooldown_ms == 0U) {
                        shooter_enemy_fire_laser(core, enemy);
                        enemy->fire_cooldown_ms = 2200U;
                    }
                }
            }
        }

        if (enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_ELITE_DASHER) {
            if (enemy->x < 60 || enemy->x > (int32_t)core->logical_width - 60) {
                enemy->vx = (int16_t)-enemy->vx;
            }
        } else if (enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_ELITE_BRUISER) {
            if (enemy->x < 80 || enemy->x > (int32_t)core->logical_width - 80) {
                enemy->vx = (int16_t)-enemy->vx;
            }
        } else if (enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_ELITE_PINNER) {
            int32_t dx = core->player.x - enemy->x;

            if (shooter_abs_i32(dx) <= 18) {
                enemy->vx = 0;
            } else {
                enemy->vx = (dx < 0) ? -130 : 130;
            }

            if (enemy->y < 180) {
                enemy->vy = 120;
            } else if (enemy->y > 250) {
                enemy->vy = -70;
            } else {
                enemy->vy = 0;
            }
        } else if (enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_BOSS && core->boss_definition != NULL) {
            uint8_t phase_index = shooter_boss_phase_for_hp(core->boss_definition, enemy->hp, enemy->max_hp);
            const shooter_boss_phase_t *phase = &core->boss_definition->phases[phase_index];

            if (phase_index != core->active_boss_phase || !core->boss_spawned) {
                core->active_boss_phase = phase_index;
                shooter_reset_boss_phase_state(core, phase);
            }

            core->boss_vulnerability_cycle_ms = (uint16_t)(core->boss_vulnerability_cycle_ms + dt_ms);
            if (core->boss_vulnerability_cycle_ms >= phase->vulnerability_cycle_ms) {
                core->boss_vulnerability_cycle_ms = 0U;
            }

            core->boss_hazard_kind = phase->hazard_kind;

            if (core->boss_attack_active) {
                core->boss_attack_duration_ms = shooter_reduce_cooldown(core->boss_attack_duration_ms, dt_ms);
                core->boss_attack_volley_cooldown_ms =
                    shooter_reduce_cooldown(core->boss_attack_volley_cooldown_ms, dt_ms);
                if (phase->pattern == SHOOTER_BOSS_PATTERN_DASH) {
                    enemy->vx = (enemy->x >= core->player.x) ? (int16_t)(-phase->attack_vx) : phase->attack_vx;
                    enemy->vy = phase->move_vy;
                } else if (phase->pattern == SHOOTER_BOSS_PATTERN_DIVE) {
                    enemy->vx = (enemy->x >= core->player.x) ? -phase->move_vx : phase->move_vx;
                    enemy->vy = phase->attack_vy;
                    if (enemy->y > 320) {
                        enemy->vy = (int16_t)(-phase->attack_vy / 2);
                    }
                } else {
                    enemy->vx = (enemy->x >= core->player.x) ? (int16_t)(-phase->attack_vx / 2) : (int16_t)(phase->attack_vx / 2);
                    enemy->vy = phase->attack_vy;
                }

                if (core->boss_hazard_telegraph_ms == 0U && core->boss_hazard_active_ms == 0U) {
                    shooter_boss_capture_hazard_anchor(core);
                    core->boss_hazard_telegraph_ms = SHOOTER_HAZARD_TELEGRAPH_MS;
                }

                if (core->boss_attack_volley_cooldown_ms == 0U) {
                    shooter_boss_fire_attack_pattern(core, enemy, phase);
                    core->boss_attack_volley_cooldown_ms = phase->attack_volley_interval_ms;
                }

                if (phase->pattern == SHOOTER_BOSS_PATTERN_DIVE && enemy->y > 320) {
                    enemy->vy = (int16_t)(-phase->attack_vy / 2);
                }
                if (core->boss_attack_duration_ms == 0U) {
                    core->boss_attack_active = false;
                    core->boss_attack_volley_cooldown_ms = 0U;
                    core->boss_recovery_ms = phase->recovery_window_ms;
                }
            } else {
                enemy->vx = phase->move_vx;
                enemy->vy = phase->move_vy;
                core->boss_attack_cooldown_ms = shooter_reduce_cooldown(core->boss_attack_cooldown_ms, dt_ms);
                if (core->boss_attack_cooldown_ms == 0U) {
                    core->boss_attack_active = true;
                    core->boss_attack_duration_ms = phase->attack_duration_ms;
                    core->boss_attack_volley_cooldown_ms = 0U;
                    core->boss_attack_cooldown_ms = phase->attack_interval_ms;
                    core->boss_attack_cycle = 0U;
                    core->boss_recovery_ms = 0U;
                }
            }

            if (core->boss_recovery_ms > 0U) {
                core->boss_recovery_ms = shooter_reduce_cooldown(core->boss_recovery_ms, dt_ms);
            }

            core->boss_vulnerable =
                (core->boss_recovery_ms > 0U) ||
                (!core->boss_attack_active &&
                 core->boss_hazard_telegraph_ms == 0U &&
                 core->boss_hazard_active_ms == 0U &&
                 core->boss_vulnerability_cycle_ms < phase->vulnerability_window_ms);

            if (enemy->x < 120 || enemy->x > (int32_t)core->logical_width - 120) {
                enemy->vx = (int16_t)-enemy->vx;
            }

            core->boss_support_cooldown_ms = shooter_reduce_cooldown(core->boss_support_cooldown_ms, dt_ms);
            if (core->boss_support_cooldown_ms == 0U) {
                uint8_t burst_count = phase->support_burst_count;
                for (uint8_t burst = 0U; burst < burst_count; ++burst) {
                    shooter_spawn_enemy_archetype(core, phase->support_type);
                }
                core->boss_support_cooldown_ms = phase->support_spawn_interval_ms;
            }
        }

        enemy->x += (enemy->vx * (int32_t)dt_ms) / 1000;
        enemy->y += (enemy->vy * (int32_t)dt_ms) / 1000;
        if (enemy->x < 40) {
            enemy->x = 40;
            enemy->vx = (int16_t)-enemy->vx;
        } else if (enemy->x > (int32_t)core->logical_width - 40) {
            enemy->x = (int32_t)core->logical_width - 40;
            enemy->vx = (int16_t)-enemy->vx;
        }

        if (enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_BOSS &&
            enemy->y > (int32_t)core->logical_height - 120) {
            enemy->y = (int32_t)core->logical_height - 120;
            if (enemy->vy > 0) {
                enemy->vy = (int16_t)(-(enemy->vy / 2) - 80);
            }
            shooter_apply_damage(core, 1U);
        }

        if (enemy->y > (int32_t)core->logical_height + 40) {
            if (enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_BOSS) {
                enemy->y = (int32_t)core->logical_height - 140;
                enemy->vy = -140;
            } else {
                enemy->active = false;
            }
        }
    }

    shooter_apply_boss_hazard(core, dt_ms);
}

static void shooter_resolve_collisions(shooter_core_t *core)
{
    uint16_t enemy_index;
    uint16_t bullet_index;
    uint16_t enemy_bullet_index;
    const int32_t player_radius = SHOOTER_PLAYER_RADIUS;

    for (enemy_index = 0U; enemy_index < SHOOTER_MAX_ENEMIES; ++enemy_index) {
        shooter_enemy_t *enemy = &core->enemies[enemy_index];
        if (!enemy->active) {
            continue;
        }

        if (shooter_overlap(enemy->x, enemy->y, enemy->radius, core->player.x, core->player.y, player_radius)) {
            shooter_apply_damage(core, enemy->contact_damage);
            if (enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_BOSS) {
                enemy->vy = -120;
                enemy->vx = (enemy->x >= core->player.x) ? 160 : -160;
                core->boss_attack_active = false;
                core->boss_recovery_ms = (core->boss_recovery_ms < 250U) ? 250U : core->boss_recovery_ms;
            } else {
                enemy->hp = 0;
                (void)shooter_finish_enemy_if_dead(core, enemy);
            }
            continue;
        }

        for (bullet_index = 0U; bullet_index < SHOOTER_MAX_PLAYER_BULLETS; ++bullet_index) {
            shooter_bullet_t *bullet = &core->bullets[bullet_index];
            int16_t damage = 1;

            if (!bullet->active) {
                continue;
            }
            if (!shooter_overlap(enemy->x, enemy->y, enemy->radius, bullet->x, bullet->y, SHOOTER_BULLET_RADIUS)) {
                continue;
            }

            damage = (bullet->damage > 0) ? bullet->damage : 1;
            if (enemy->archetype == (uint8_t)SHOOTER_ENEMY_ARCHETYPE_BOSS && !core->boss_vulnerable) {
                damage = 0;
            }
            enemy->hp -= damage;
            if ((bullet->flags & SHOOTER_BULLET_FLAG_PIERCE) == 0U) {
                bullet->active = false;
            } else {
                bullet->flags &= (uint8_t)~SHOOTER_BULLET_FLAG_PIERCE;
            }
            (void)shooter_finish_enemy_if_dead(core, enemy);
            break;
        }
    }

    for (enemy_bullet_index = 0U; enemy_bullet_index < SHOOTER_MAX_ENEMY_BULLETS; ++enemy_bullet_index) {
        shooter_bullet_t *bullet = &core->enemy_bullets[enemy_bullet_index];
        if (!bullet->active) {
            continue;
        }
        if (core->shield_active) {
            if ((bullet->flags & SHOOTER_BULLET_FLAG_LASER) != 0U) {
                int32_t laser_left = bullet->x - 12;
                int32_t laser_right = bullet->x + 12;
                int32_t shield_radius = 80;
                int32_t shield_left = core->player.x - shield_radius;
                int32_t shield_right = core->player.x + shield_radius;
                if (shield_right >= laser_left && shield_left <= laser_right &&
                    core->player.y + shield_radius >= bullet->y) {
                    bullet->active = false;
                    continue;
                }
            } else {
                if (shooter_overlap(bullet->x, bullet->y, SHOOTER_ENEMY_BULLET_RADIUS,
                                    core->player.x, core->player.y, 80)) {
                    bullet->active = false;
                    continue;
                }
            }
        }
        if ((bullet->flags & SHOOTER_BULLET_FLAG_LASER) != 0U) {
            int32_t laser_half = 14;
            int32_t dx = core->player.x - bullet->x;
            if (dx < 0) dx = -dx;
            if (dx <= laser_half && core->player.y >= bullet->y) {
                bullet->active = false;
                shooter_apply_damage(core, 1U);
            }
        } else {
            if (!shooter_overlap(bullet->x, bullet->y, SHOOTER_ENEMY_BULLET_RADIUS,
                                 core->player.x, core->player.y, player_radius)) {
                continue;
            }
            bullet->active = false;
            shooter_apply_damage(core, 1U);
        }
    }
}

static void shooter_update_phase(shooter_core_t *core)
{
    if (core->player.hp <= 0) {
        core->phase = SHOOTER_PHASE_LOST;
        return;
    }

    if (core->stage_config == NULL) {
        return;
    }

    if (core->stage_config->victory_mode == SHOOTER_VICTORY_BOSS) {
        if (core->stage_script_complete) {
            core->phase = SHOOTER_PHASE_WON;
        }
        return;
    }

    if (core->stage_script_complete && shooter_core_count_active_enemies(core) == 0U) {
        core->phase = SHOOTER_PHASE_WON;
    }
}

static void shooter_update_explosions(shooter_core_t *core, uint32_t dt_ms)
{
    uint16_t i;
    for (i = 0U; i < SHOOTER_MAX_EXPLOSIONS; ++i) {
        if (core->explosions[i].active) {
            core->explosions[i].age_ms = (uint16_t)(core->explosions[i].age_ms + dt_ms);
            if (core->explosions[i].age_ms >= 360U) {
                core->explosions[i].active = false;
            }
        }
    }
}

void shooter_core_init(shooter_core_t *core, uint32_t level_id)
{
    if (core == NULL) {
        return;
    }

    memset(core, 0, sizeof(*core));
    core->version = SHOOTER_CORE_VERSION;
    core->logical_width = SHOOTER_LOGICAL_WIDTH;
    core->logical_height = SHOOTER_LOGICAL_HEIGHT;
    core->phase = SHOOTER_PHASE_RUNNING;
    core->player.x = SHOOTER_LOGICAL_WIDTH / 2U;
    core->player.y = SHOOTER_PLAYER_START_Y;
    shooter_core_configure_level(core, level_id);
}

void shooter_core_step(shooter_core_t *core, const shooter_input_state_t *input, uint32_t dt_ms)
{
    if (core == NULL || dt_ms == 0U) {
        return;
    }

    if (input != NULL && input->pause_pressed) {
        if (core->phase == SHOOTER_PHASE_RUNNING) {
            core->phase = SHOOTER_PHASE_PAUSED;
        } else if (core->phase == SHOOTER_PHASE_PAUSED) {
            core->phase = SHOOTER_PHASE_RUNNING;
        }
    }
    if (core->phase != SHOOTER_PHASE_RUNNING) {
        return;
    }

    core->fire_edge_pressed = false;
    core->device_edge_pressed = false;
    if (input != NULL) {
        core->fire_edge_pressed = input->fire_pressed && !core->fire_held_last_frame;
        core->device_edge_pressed = input->device_pressed && !core->device_held_last_frame;
        core->fire_held_last_frame = input->fire_pressed;
        core->device_held_last_frame = input->device_pressed;
    } else {
        core->fire_held_last_frame = false;
        core->device_held_last_frame = false;
    }

    core->beam_active_ms = shooter_reduce_cooldown(core->beam_active_ms, dt_ms);
    core->feedback_damage_flash_ms = shooter_reduce_cooldown(core->feedback_damage_flash_ms, dt_ms);
    core->feedback_kill_flash_ms = shooter_reduce_cooldown(core->feedback_kill_flash_ms, dt_ms);
    core->feedback_danger_flash_ms = shooter_reduce_cooldown(core->feedback_danger_flash_ms, dt_ms);
    core->feedback_device_flash_ms = shooter_reduce_cooldown(core->feedback_device_flash_ms, dt_ms);

    if (core->shield_active) {
        if (dt_ms >= core->shield_duration_ms) {
            core->shield_duration_ms = 0U;
            core->shield_active = false;
        } else {
            core->shield_duration_ms -= (uint16_t)dt_ms;
        }
    }

    if (core->player.hp == 1 && core->feedback_danger_flash_ms == 0U) {
        core->feedback_danger_flash_ms = SHOOTER_FEEDBACK_DANGER_MS;
    }

    core->elapsed_ms += dt_ms;
    shooter_update_player(core, input, dt_ms);
    shooter_activate_device(core);
    shooter_update_bullets(core, dt_ms);
    shooter_update_enemies(core, dt_ms);
    shooter_resolve_beam_hits(core);
    shooter_update_explosions(core, dt_ms);
    shooter_resolve_collisions(core);
    shooter_update_phase(core);
}

uint16_t shooter_core_count_active_bullets(const shooter_core_t *core)
{
    uint16_t count = 0U;
    uint16_t i;
    if (core == NULL) {
        return 0U;
    }
    for (i = 0U; i < SHOOTER_MAX_PLAYER_BULLETS; ++i) {
        if (core->bullets[i].active) {
            count += 1U;
        }
    }
    return count;
}

uint16_t shooter_core_count_active_enemies(const shooter_core_t *core)
{
    uint16_t count = 0U;
    uint16_t i;
    if (core == NULL) {
        return 0U;
    }
    for (i = 0U; i < SHOOTER_MAX_ENEMIES; ++i) {
        if (core->enemies[i].active) {
            count += 1U;
        }
    }
    return count;
}

const char *shooter_core_phase_name(shooter_phase_t phase)
{
    switch (phase) {
    case SHOOTER_PHASE_RUNNING:
        return "作战中";
    case SHOOTER_PHASE_PAUSED:
        return "暂停";
    case SHOOTER_PHASE_WON:
        return "胜利";
    case SHOOTER_PHASE_LOST:
        return "失败";
    default:
        return "未知";
    }
}

const char *shooter_core_stage_title(const shooter_core_t *core)
{
    if (core == NULL || core->stage_config == NULL) {
        return "未知关卡";
    }
    return core->stage_config->title;
}

const char *shooter_core_objective_text(const shooter_core_t *core)
{
    if (core == NULL || core->stage_config == NULL) {
        return "无目标";
    }
    return core->stage_config->objective;
}

const char *shooter_core_segment_label(const shooter_core_t *core)
{
    if (core == NULL || core->wave_plan == NULL || core->segment_index >= core->wave_plan->segment_count) {
        return "已清空";
    }
    return core->wave_plan->segments[core->segment_index].label;
}
