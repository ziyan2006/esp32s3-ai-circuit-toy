#include "shooter_boss.h"

#include <stddef.h>

static const shooter_boss_definition_t s_xor_reactor = {
    "异或反应堆",
    "异或",
    18U,
    3U,
    {
        {"阶段1：分歧", 100U, 150, 18, 220, 32, 2100U, 2200U, 900U, 1400U, 640U, 780U, 210U, 1U, 1U,
         SHOOTER_BOSS_PATTERN_PATROL, SHOOTER_BOSS_ATTACK_WIDE_FAN, SHOOTER_BOSS_ATTACK_CROSS_BURST,
         SHOOTER_HAZARD_TARGET_COLUMN, SHOOTER_ENEMY_ARCHETYPE_GRUNT},
        {"阶段2：分裂脉冲", 65U, 220, 28, 340, 54, 1500U, 1800U, 650U, 1050U, 760U, 620U, 180U, 2U, 2U,
         SHOOTER_BOSS_PATTERN_DASH, SHOOTER_BOSS_ATTACK_CROSS_BURST, SHOOTER_BOSS_ATTACK_SPIRAL_SWEEP,
         SHOOTER_HAZARD_EDGE_WALLS, SHOOTER_ENEMY_ARCHETYPE_ELITE_DASHER},
        {"阶段3：坍缩", 30U, 280, 40, 0, 360, 1050U, 1500U, 450U, 760U, 920U, 480U, 150U, 2U, 3U,
         SHOOTER_BOSS_PATTERN_DIVE, SHOOTER_BOSS_ATTACK_SPIRAL_SWEEP, SHOOTER_BOSS_ATTACK_CROSS_BURST,
         SHOOTER_HAZARD_BOTTOM_SWEEP, SHOOTER_ENEMY_ARCHETYPE_ELITE_BRUISER},
    }
};

static const shooter_boss_definition_t s_and_fortress = {
    "与门要塞",
    "与门",
    28U,
    3U,
    {
        {"阶段1：墙锁", 100U, 120, 12, 180, 18, 2100U, 2400U, 850U, 1500U, 620U, 860U, 230U, 1U, 1U,
         SHOOTER_BOSS_PATTERN_PATROL, SHOOTER_BOSS_ATTACK_GATE_VOLLEY, SHOOTER_BOSS_ATTACK_WIDE_FAN,
         SHOOTER_HAZARD_GATE_PAIR, SHOOTER_ENEMY_ARCHETYPE_GRUNT},
        {"阶段2：夹持网格", 60U, 180, 22, 260, 24, 1450U, 1800U, 600U, 1180U, 760U, 640U, 190U, 2U, 2U,
         SHOOTER_BOSS_PATTERN_DASH, SHOOTER_BOSS_ATTACK_GATE_VOLLEY, SHOOTER_BOSS_ATTACK_WIDE_FAN,
         SHOOTER_HAZARD_CENTER_WALL, SHOOTER_ENEMY_ARCHETYPE_ELITE_BRUISER},
        {"阶段3：终极门阵", 28U, 240, 32, 0, 320, 950U, 1500U, 420U, 860U, 980U, 460U, 150U, 3U, 3U,
         SHOOTER_BOSS_PATTERN_DIVE, SHOOTER_BOSS_ATTACK_GATE_VOLLEY, SHOOTER_BOSS_ATTACK_WIDE_FAN,
         SHOOTER_HAZARD_EDGE_WALLS, SHOOTER_ENEMY_ARCHETYPE_ELITE_PINNER},
    }
};

const shooter_boss_definition_t *shooter_boss_get(uint32_t level_id)
{
    switch (level_id) {
    case 5U:
        return &s_xor_reactor;
    case 6U:
        return &s_and_fortress;
    default:
        return NULL;
    }
}

uint8_t shooter_boss_phase_for_hp(const shooter_boss_definition_t *definition, int16_t hp, uint16_t max_hp)
{
    uint8_t i;
    uint32_t hp_percent;

    if (definition == NULL || max_hp == 0U) {
        return 0U;
    }

    if (hp < 0) {
        hp = 0;
    }

    hp_percent = ((uint32_t)hp * 100U) / max_hp;
    for (i = 0U; i + 1U < definition->phase_count; ++i) {
        if (hp_percent > definition->phases[i + 1U].hp_percent_threshold) {
            return i;
        }
    }

    return (definition->phase_count == 0U) ? 0U : (uint8_t)(definition->phase_count - 1U);
}
