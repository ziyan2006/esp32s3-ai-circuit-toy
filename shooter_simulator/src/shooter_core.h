#ifndef SHOOTER_CORE_H
#define SHOOTER_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "shooter_boss.h"
#include "shooter_stage.h"
#include "shooter_wave.h"

#define SHOOTER_MAX_PLAYER_BULLETS 24
#define SHOOTER_MAX_ENEMIES 12
#define SHOOTER_MAX_ENEMY_BULLETS 32
#define SHOOTER_MAX_EXPLOSIONS 8

typedef struct {
    int16_t move_x;
    int16_t move_y;
    bool fire_pressed;
    bool device_pressed;
    bool pause_pressed;
} shooter_input_state_t;

typedef enum {
    SHOOTER_PHASE_RUNNING = 0,
    SHOOTER_PHASE_PAUSED,
    SHOOTER_PHASE_WON,
    SHOOTER_PHASE_LOST,
} shooter_phase_t;

typedef struct {
    int32_t x;
    int32_t y;
    int16_t hp;
    uint16_t fire_cooldown_ms;
} shooter_player_t;

typedef struct {
    bool active;
    int32_t x;
    int32_t y;
    int16_t vx;
    int16_t vy;
    int16_t damage;
    uint8_t ttl_ticks;
    uint8_t flags;
} shooter_bullet_t;

#define SHOOTER_BULLET_FLAG_PIERCE 0x01U
#define SHOOTER_BULLET_FLAG_LASER 0x08U

typedef enum {
    SHOOTER_ENEMY_TYPE_STRAIGHT = 0,
    SHOOTER_ENEMY_TYPE_DIVER,
    SHOOTER_ENEMY_TYPE_TRACKER,
    SHOOTER_ENEMY_TYPE_LASER,
} shooter_enemy_type_t;

typedef struct {
    bool active;
    int32_t x;
    int32_t y;
    int16_t vx;
    int16_t vy;
    int16_t hp;
    uint16_t max_hp;
    uint8_t radius;
    uint8_t contact_damage;
    uint8_t archetype;
    uint8_t type;
    uint8_t behavior_state;
    uint16_t fire_cooldown_ms;
    uint16_t behavior_timer_ms;
    uint32_t last_beam_cast_id;
} shooter_enemy_t;

typedef struct {
    bool active;
    int32_t x;
    int32_t y;
    uint16_t age_ms;
} shooter_explosion_t;

typedef struct {
    uint16_t fire_cooldown_ms;
    uint16_t spawn_cooldown_ms;
    uint16_t device_cooldown_ms;
    int16_t bullet_speed;
    int16_t enemy_speed;
    uint16_t wave_budget;
    uint16_t player_max_hp;
} shooter_balance_profile_t;

typedef struct {
    uint32_t version;
    uint16_t logical_width;
    uint16_t logical_height;
    uint32_t elapsed_ms;
    uint16_t total_spawned;
    uint16_t total_destroyed;
    uint16_t total_hits_taken;
    uint16_t total_device_activations;
    uint16_t spawn_cooldown_ms;
    uint16_t spawn_cooldown_base_ms;
    uint16_t wave_budget_remaining;
    uint16_t wave_budget_total;
    uint8_t spawn_wave_type;
    uint8_t spawn_wave_step;
    uint8_t spawn_wave_remaining;
    uint8_t spawn_wave_is_edge_pressure;
    uint8_t spawn_wave_edge_side;
    uint16_t completed_waves;
    uint16_t edge_pressure_waves;
    uint16_t device_cooldown_ms;
    uint16_t device_cooldown_base_ms;
    uint32_t level;
    bool shield_active;
    uint16_t shield_duration_ms;
    uint16_t beam_active_ms;
    uint32_t beam_cast_id;
    bool fire_edge_pressed;
    bool fire_held_last_frame;
    bool device_edge_pressed;
    bool device_held_last_frame;
    shooter_phase_t phase;
    shooter_player_t player;
    shooter_bullet_t bullets[SHOOTER_MAX_PLAYER_BULLETS];
    shooter_bullet_t enemy_bullets[SHOOTER_MAX_ENEMY_BULLETS];
    shooter_enemy_t enemies[SHOOTER_MAX_ENEMIES];
    shooter_explosion_t explosions[SHOOTER_MAX_EXPLOSIONS];
    shooter_upgrade_profile_t upgrades;
    uint16_t feedback_damage_flash_ms;
    uint16_t feedback_kill_flash_ms;
    uint16_t feedback_danger_flash_ms;
    uint16_t feedback_device_flash_ms;
    shooter_balance_profile_t balance;
    const shooter_stage_config_t *stage_config;
    const shooter_wave_plan_t *wave_plan;
    const shooter_boss_definition_t *boss_definition;
    uint16_t stage_spawn_budget_total;
    uint16_t stage_spawn_budget_spent;
    uint16_t segment_elapsed_ms;
    uint16_t segment_spawned;
    uint16_t boss_support_cooldown_ms;
    uint16_t boss_attack_cooldown_ms;
    uint16_t boss_attack_duration_ms;
    uint16_t boss_attack_volley_cooldown_ms;
    uint16_t boss_recovery_ms;
    uint16_t boss_vulnerability_cycle_ms;
    uint16_t boss_hazard_telegraph_ms;
    uint16_t boss_hazard_active_ms;
    uint8_t boss_support_burst_remaining;
    uint8_t segment_index;
    uint8_t active_boss_phase;
    uint8_t boss_attack_variant;
    uint8_t boss_attack_cycle;
    shooter_hazard_kind_t boss_hazard_kind;
    bool boss_attack_active;
    bool boss_vulnerable;
    bool boss_spawned;
    bool boss_defeated;
    bool stage_script_complete;
} shooter_core_t;

void shooter_core_init(shooter_core_t *core, uint32_t level_id);
void shooter_core_configure_level(shooter_core_t *core, uint32_t level_id);
void shooter_core_step(shooter_core_t *core, const shooter_input_state_t *input, uint32_t dt_ms);
uint16_t shooter_core_count_active_bullets(const shooter_core_t *core);
uint16_t shooter_core_count_active_enemies(const shooter_core_t *core);
const char *shooter_core_phase_name(shooter_phase_t phase);
const char *shooter_core_stage_title(const shooter_core_t *core);
const char *shooter_core_objective_text(const shooter_core_t *core);
const char *shooter_core_segment_label(const shooter_core_t *core);
void shooter_core_run_self_tests(void);

#endif
