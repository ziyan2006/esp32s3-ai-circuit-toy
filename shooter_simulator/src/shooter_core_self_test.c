#include "shooter_core.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stddef.h>

static void suppress_wave_spawns(shooter_core_t *core)
{
    core->spawn_cooldown_ms = 60000U;
}

static void release_inputs(shooter_core_t *core)
{
    shooter_input_state_t input = {0};
    shooter_core_step(core, &input, 1U);
}

static void test_fixed_upgrade_profiles(void)
{
    static const uint8_t cores[] = {3U, 3U, 3U, 3U, 4U, 5U};
    static const uint8_t propulsion[] = {0U, 1U, 1U, 2U, 2U, 2U};
    static const uint8_t damage_levels[] = {0U, 1U, 1U, 1U, 3U, 3U};
    static const uint8_t fire_levels[] = {0U, 0U, 1U, 2U, 2U, 2U};
    static const uint8_t projectiles[] = {1U, 1U, 2U, 2U, 3U, 4U};
    static const int16_t damage[] = {1, 2, 2, 2, 4, 4};
    static const uint16_t interval[] = {320U, 320U, 240U, 180U, 180U, 180U};

    for (uint32_t level = 1U; level <= 6U; ++level) {
        const shooter_stage_config_t *stage = shooter_stage_get(level);
        const uint32_t i = level - 1U;
        assert(stage->level_id == level);
        assert(stage->upgrades.core_count == cores[i]);
        assert(stage->upgrades.propulsion_level == propulsion[i]);
        assert(stage->upgrades.damage_level == damage_levels[i]);
        assert(stage->upgrades.fire_rate_level == fire_levels[i]);
        assert(stage->upgrades.projectile_count == projectiles[i]);
        assert(stage->upgrades.bullet_damage == damage[i]);
        assert(stage->upgrades.fire_interval_ms == interval[i]);
        assert(stage->upgrades.shield_unlocked == (level >= 2U));
        assert(stage->upgrades.beam_unlocked == (level >= 4U));
    }

    assert(shooter_stage_get(0U)->level_id == 1U);
    assert(shooter_stage_get(7U)->level_id == 1U);
    assert(shooter_wave_get_plan(0U) == shooter_wave_get_plan(1U));
}

static void test_initial_state_uses_stage_profile(void)
{
    shooter_core_t core;
    shooter_core_init(&core, 6U);

    assert(core.version == 4U);
    assert(core.level == 6U);
    assert(core.player.hp == 5);
    assert(core.balance.player_max_hp == 5U);
    assert(core.upgrades.projectile_count == 4U);
    assert(core.device_cooldown_base_ms == 10000U);
    assert(core.phase == SHOOTER_PHASE_RUNNING);
}

static void test_manual_fire_requires_new_press(void)
{
    shooter_core_t core;
    shooter_input_state_t input = {0};

    shooter_core_init(&core, 1U);
    suppress_wave_spawns(&core);
    shooter_core_step(&core, &input, 16U);
    assert(shooter_core_count_active_bullets(&core) == 0U);

    input.fire_pressed = true;
    shooter_core_step(&core, &input, 16U);
    assert(shooter_core_count_active_bullets(&core) == 1U);
    assert(core.bullets[0].damage == 1);

    shooter_core_step(&core, &input, 400U);
    assert(shooter_core_count_active_bullets(&core) == 1U);

    input.fire_pressed = false;
    shooter_core_step(&core, &input, 1U);
    input.fire_pressed = true;
    shooter_core_step(&core, &input, 1U);
    assert(shooter_core_count_active_bullets(&core) == 2U);
}

static void test_press_during_cooldown_is_discarded(void)
{
    shooter_core_t core;
    shooter_input_state_t input = {.fire_pressed = true};

    shooter_core_init(&core, 1U);
    suppress_wave_spawns(&core);
    shooter_core_step(&core, &input, 16U);
    assert(shooter_core_count_active_bullets(&core) == 1U);

    release_inputs(&core);
    input.fire_pressed = true;
    shooter_core_step(&core, &input, 16U);
    assert(shooter_core_count_active_bullets(&core) == 1U);

    input.fire_pressed = false;
    shooter_core_step(&core, &input, 400U);
    assert(shooter_core_count_active_bullets(&core) == 1U);
}

static void test_multishot_and_damage_follow_level(void)
{
    shooter_core_t core;
    shooter_input_state_t input = {.fire_pressed = true};

    shooter_core_init(&core, 6U);
    suppress_wave_spawns(&core);
    shooter_core_step(&core, &input, 16U);
    assert(shooter_core_count_active_bullets(&core) == 4U);
    for (uint16_t i = 0U; i < 4U; ++i) {
        assert(core.bullets[i].damage == 4);
        assert(core.bullets[i].vx == 0);
    }
    assert(core.bullets[0].x < core.bullets[1].x);
    assert(core.bullets[1].x < core.bullets[2].x);
    assert(core.bullets[2].x < core.bullets[3].x);
}

static void test_propulsion_upgrade_changes_speed(void)
{
    shooter_core_t level1;
    shooter_core_t level4;
    shooter_input_state_t input = {.move_x = 1};

    shooter_core_init(&level1, 1U);
    shooter_core_init(&level4, 4U);
    suppress_wave_spawns(&level1);
    suppress_wave_spawns(&level4);
    shooter_core_step(&level1, &input, 100U);
    shooter_core_step(&level4, &input, 100U);
    assert(level4.player.x > level1.player.x);
}

static void test_stage_one_has_no_device(void)
{
    shooter_core_t core;
    shooter_input_state_t input = {.device_pressed = true};

    shooter_core_init(&core, 1U);
    suppress_wave_spawns(&core);
    shooter_core_step(&core, &input, 16U);
    assert(!core.shield_active);
    assert(core.beam_active_ms == 0U);
    assert(core.device_cooldown_ms == 0U);
}

static void test_shield_duration_and_immunity(void)
{
    shooter_core_t core;
    shooter_input_state_t input = {.device_pressed = true};

    shooter_core_init(&core, 2U);
    suppress_wave_spawns(&core);
    shooter_core_step(&core, &input, 16U);
    assert(core.shield_active);
    assert(core.shield_duration_ms == 3000U);
    assert(core.device_cooldown_ms == 10000U);
    assert(core.total_device_activations == 1U);

    input.device_pressed = false;
    core.enemy_bullets[0].active = true;
    core.enemy_bullets[0].x = core.player.x;
    core.enemy_bullets[0].y = core.player.y;
    shooter_core_step(&core, &input, 16U);
    assert(core.player.hp == 3);
    assert(!core.enemy_bullets[0].active);

    shooter_core_step(&core, &input, 2984U);
    assert(!core.shield_active);
    core.enemy_bullets[0].active = true;
    core.enemy_bullets[0].x = core.player.x;
    core.enemy_bullets[0].y = core.player.y;
    shooter_core_step(&core, &input, 16U);
    assert(core.player.hp == 2);
}

static void prepare_beam_enemy(shooter_enemy_t *enemy, int32_t x, int32_t y, int16_t hp)
{
    *enemy = (shooter_enemy_t) {
        .active = true,
        .x = x,
        .y = y,
        .hp = hp,
        .max_hp = (uint16_t)hp,
        .radius = 22U,
        .archetype = SHOOTER_ENEMY_ARCHETYPE_GRUNT,
        .type = SHOOTER_ENEMY_TYPE_STRAIGHT,
        .fire_cooldown_ms = 60000U,
    };
}

static void test_beam_hits_each_enemy_once_per_cast(void)
{
    shooter_core_t core;
    shooter_input_state_t input = {.device_pressed = true};

    shooter_core_init(&core, 4U);
    suppress_wave_spawns(&core);
    prepare_beam_enemy(&core.enemies[0], core.player.x, core.player.y - 120, 20);
    shooter_core_step(&core, &input, 16U);
    assert(core.beam_active_ms > 0U);
    assert(core.enemies[0].hp == 14);

    input.device_pressed = false;
    shooter_core_step(&core, &input, 16U);
    assert(core.enemies[0].hp == 14);

    prepare_beam_enemy(&core.enemies[1], core.player.x + 80, core.player.y - 100, 20);
    shooter_core_step(&core, &input, 16U);
    assert(core.enemies[1].hp == 14);
}

static void test_beam_uses_current_bullet_damage(void)
{
    shooter_core_t core;
    shooter_input_state_t input = {.device_pressed = true};

    shooter_core_init(&core, 5U);
    suppress_wave_spawns(&core);
    prepare_beam_enemy(&core.enemies[0], core.player.x, core.player.y - 100, 30);
    shooter_core_step(&core, &input, 16U);
    assert(core.enemies[0].hp == 18);
}

static void test_device_requires_release_and_ten_second_cooldown(void)
{
    shooter_core_t core;
    shooter_input_state_t input = {.device_pressed = true};

    shooter_core_init(&core, 3U);
    suppress_wave_spawns(&core);
    shooter_core_step(&core, &input, 16U);
    assert(core.total_device_activations == 1U);

    shooter_core_step(&core, &input, 10000U);
    assert(core.total_device_activations == 1U);
    input.device_pressed = false;
    shooter_core_step(&core, &input, 1U);
    input.device_pressed = true;
    shooter_core_step(&core, &input, 1U);
    assert(core.total_device_activations == 2U);
}

static void test_boss_levels_remain_mapped(void)
{
    shooter_core_t stage5;
    shooter_core_t stage6;
    shooter_core_init(&stage5, 5U);
    shooter_core_init(&stage6, 6U);
    assert(stage5.boss_definition != NULL);
    assert(stage6.boss_definition != NULL);
    assert(stage5.wave_plan->segments[stage5.wave_plan->segment_count - 1U].tier == SHOOTER_WAVE_TIER_BOSS);
    assert(stage6.wave_plan->segments[stage6.wave_plan->segment_count - 1U].tier == SHOOTER_WAVE_TIER_BOSS);
    assert(stage5.boss_definition->max_hp == 18U);
    assert(stage6.boss_definition->max_hp == 28U);
}

static const shooter_enemy_t *find_enemy(const shooter_core_t *core,
                                         shooter_enemy_archetype_t archetype)
{
    for (uint16_t i = 0U; i < SHOOTER_MAX_ENEMIES; ++i) {
        if (core->enemies[i].active && core->enemies[i].archetype == (uint8_t)archetype) {
            return &core->enemies[i];
        }
    }
    return NULL;
}

static void test_strengthened_enemy_runtime_hp(void)
{
    shooter_core_t stage3;
    shooter_core_t stage6;
    shooter_input_state_t input = {0};

    shooter_core_init(&stage3, 3U);
    stage3.segment_index = 1U;
    stage3.segment_spawned = 2U;
    stage3.spawn_cooldown_ms = 0U;
    shooter_core_step(&stage3, &input, 1U);
    const shooter_enemy_t *early_bruiser = find_enemy(&stage3, SHOOTER_ENEMY_ARCHETYPE_ELITE_BRUISER);
    assert(early_bruiser != NULL);
    assert(early_bruiser->max_hp == 12U);

    shooter_core_init(&stage6, 6U);
    stage6.segment_index = 1U;
    stage6.segment_spawned = 2U;
    stage6.spawn_cooldown_ms = 0U;
    shooter_core_step(&stage6, &input, 1U);
    const shooter_enemy_t *late_bruiser = find_enemy(&stage6, SHOOTER_ENEMY_ARCHETYPE_ELITE_BRUISER);
    assert(late_bruiser != NULL);
    assert(late_bruiser->max_hp == 21U);

    shooter_core_init(&stage6, 6U);
    stage6.segment_index = (uint8_t)(stage6.wave_plan->segment_count - 1U);
    shooter_core_step(&stage6, &input, 1U);
    const shooter_enemy_t *boss = find_enemy(&stage6, SHOOTER_ENEMY_ARCHETYPE_BOSS);
    assert(boss != NULL);
    assert(boss->max_hp == 84U);
}

void shooter_core_run_self_tests(void)
{
    test_fixed_upgrade_profiles();
    test_initial_state_uses_stage_profile();
    test_manual_fire_requires_new_press();
    test_press_during_cooldown_is_discarded();
    test_multishot_and_damage_follow_level();
    test_propulsion_upgrade_changes_speed();
    test_stage_one_has_no_device();
    test_shield_duration_and_immunity();
    test_beam_hits_each_enemy_once_per_cast();
    test_beam_uses_current_bullet_damage();
    test_device_requires_release_and_ten_second_cooldown();
    test_boss_levels_remain_mapped();
    test_strengthened_enemy_runtime_hp();
}
