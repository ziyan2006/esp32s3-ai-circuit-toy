#ifndef SHOOTER_BOSS_H
#define SHOOTER_BOSS_H

#include <stdint.h>

#include "shooter_wave.h"

#define SHOOTER_BOSS_MAX_PHASES 3

typedef enum {
    SHOOTER_HAZARD_NONE = 0,
    SHOOTER_HAZARD_TARGET_COLUMN,
    SHOOTER_HAZARD_EDGE_WALLS,
    SHOOTER_HAZARD_BOTTOM_SWEEP,
    SHOOTER_HAZARD_GATE_PAIR,
    SHOOTER_HAZARD_CENTER_WALL,
} shooter_hazard_kind_t;

typedef enum {
    SHOOTER_BOSS_PATTERN_PATROL = 0,
    SHOOTER_BOSS_PATTERN_DASH,
    SHOOTER_BOSS_PATTERN_DIVE,
} shooter_boss_pattern_t;

typedef enum {
    SHOOTER_BOSS_ATTACK_NONE = 0,
    SHOOTER_BOSS_ATTACK_WIDE_FAN,
    SHOOTER_BOSS_ATTACK_CROSS_BURST,
    SHOOTER_BOSS_ATTACK_SPIRAL_SWEEP,
    SHOOTER_BOSS_ATTACK_GATE_VOLLEY,
} shooter_boss_attack_style_t;

typedef struct {
    const char *label;
    uint8_t hp_percent_threshold;
    int16_t move_vx;
    int16_t move_vy;
    int16_t attack_vx;
    int16_t attack_vy;
    uint16_t support_spawn_interval_ms;
    uint16_t vulnerability_cycle_ms;
    uint16_t vulnerability_window_ms;
    uint16_t attack_interval_ms;
    uint16_t attack_duration_ms;
    uint16_t recovery_window_ms;
    uint16_t attack_volley_interval_ms;
    uint8_t support_burst_count;
    uint8_t attack_intensity;
    shooter_boss_pattern_t pattern;
    shooter_boss_attack_style_t primary_attack_style;
    shooter_boss_attack_style_t secondary_attack_style;
    shooter_hazard_kind_t hazard_kind;
    shooter_enemy_archetype_t support_type;
} shooter_boss_phase_t;

typedef struct {
    const char *name;
    const char *theme;
    uint16_t max_hp;
    uint8_t phase_count;
    shooter_boss_phase_t phases[SHOOTER_BOSS_MAX_PHASES];
} shooter_boss_definition_t;

const shooter_boss_definition_t *shooter_boss_get(uint32_t level_id);
uint8_t shooter_boss_phase_for_hp(const shooter_boss_definition_t *definition, int16_t hp, uint16_t max_hp);

#endif
