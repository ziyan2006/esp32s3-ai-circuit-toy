#ifndef SHOOTER_WAVE_H
#define SHOOTER_WAVE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SHOOTER_WAVE_TIER_NORMAL = 0,
    SHOOTER_WAVE_TIER_PRESSURE,
    SHOOTER_WAVE_TIER_BREATHER,
    SHOOTER_WAVE_TIER_BOSS,
} shooter_wave_tier_t;

typedef enum {
    SHOOTER_ENEMY_ARCHETYPE_GRUNT = 0,
    SHOOTER_ENEMY_ARCHETYPE_ELITE_DASHER,
    SHOOTER_ENEMY_ARCHETYPE_ELITE_BRUISER,
    SHOOTER_ENEMY_ARCHETYPE_ELITE_PINNER,
    SHOOTER_ENEMY_ARCHETYPE_BOSS,
    SHOOTER_ENEMY_ARCHETYPE_LASER_DRONE,
} shooter_enemy_archetype_t;

typedef struct {
    shooter_wave_tier_t tier;
    const char *label;
    uint16_t duration_ms;
    uint16_t spawn_interval_ms;
    uint8_t spawn_count;
    uint8_t elite_every;
    shooter_enemy_archetype_t elite_type;
    bool wait_for_clear;
} shooter_wave_segment_t;

typedef struct {
    const char *plan_name;
    uint8_t segment_count;
    const shooter_wave_segment_t *segments;
} shooter_wave_plan_t;

const shooter_wave_plan_t *shooter_wave_get_plan(uint32_t level_id);
const char *shooter_wave_tier_name(shooter_wave_tier_t tier);

#endif
