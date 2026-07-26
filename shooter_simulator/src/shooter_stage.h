#ifndef SHOOTER_STAGE_H
#define SHOOTER_STAGE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SHOOTER_VICTORY_WAVES = 0,
    SHOOTER_VICTORY_BOSS,
} shooter_victory_mode_t;

typedef struct {
    uint8_t core_count;
    uint8_t propulsion_level;
    uint8_t damage_level;
    uint8_t fire_rate_level;
    uint8_t projectile_count;
    bool shield_unlocked;
    bool beam_unlocked;
    uint16_t fire_interval_ms;
    int16_t player_speed;
    int16_t bullet_damage;
} shooter_upgrade_profile_t;

typedef struct {
    uint32_t level_id;
    const char *title;
    const char *objective;
    shooter_victory_mode_t victory_mode;
    bool has_boss;
    shooter_upgrade_profile_t upgrades;
} shooter_stage_config_t;

const shooter_stage_config_t *shooter_stage_get(uint32_t level_id);
const char *shooter_stage_victory_name(shooter_victory_mode_t mode);

#endif
