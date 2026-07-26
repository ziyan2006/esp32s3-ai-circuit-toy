#include "shooter_stage.h"

#define PROFILE(cores, propulsion, damage, fire_rate, projectiles, shield, beam, interval, speed, shot_damage) \
    {cores, propulsion, damage, fire_rate, projectiles, shield, beam, interval, speed, shot_damage}

static const shooter_stage_config_t s_stage_configs[] = {
    {1U, "第1关：非门熔炉", "熟悉手动发射，击退基础巡逻编队。",
     SHOOTER_VICTORY_WAVES, false, PROFILE(3U, 0U, 0U, 0U, 1U, false, false, 320U, 320, 1)},
    {2U, "第2关：加法器航道", "利用强化火力和星光护盾突破俯冲编队。",
     SHOOTER_VICTORY_WAVES, false, PROFILE(3U, 1U, 1U, 0U, 1U, true, false, 320U, 360, 2)},
    {3U, "第3关：与门堡垒", "使用双发光束迎战持续追踪的敌机。",
     SHOOTER_VICTORY_WAVES, false, PROFILE(3U, 1U, 1U, 1U, 2U, true, false, 240U, 360, 2)},
    {4U, "第4关：奇偶回路", "依靠二级推进和快速双发穿越夹击区域。",
     SHOOTER_VICTORY_WAVES, false, PROFILE(3U, 2U, 1U, 2U, 2U, true, true, 180U, 400, 2)},
    {5U, "第5关：异或反应堆", "以三发强化光束和贯星能量炮击破反应堆首领。",
     SHOOTER_VICTORY_BOSS, true, PROFILE(4U, 2U, 3U, 2U, 3U, true, true, 180U, 400, 4)},
    {6U, "第6关：与门要塞", "驾驶完全强化的图灵号攻克最终要塞。",
     SHOOTER_VICTORY_BOSS, true, PROFILE(5U, 2U, 3U, 2U, 4U, true, true, 180U, 400, 4)},
};

const shooter_stage_config_t *shooter_stage_get(uint32_t level_id)
{
    if (level_id < 1U || level_id > (uint32_t)(sizeof(s_stage_configs) / sizeof(s_stage_configs[0]))) {
        level_id = 1U;
    }

    return &s_stage_configs[level_id - 1U];
}

const char *shooter_stage_victory_name(shooter_victory_mode_t mode)
{
    switch (mode) {
    case SHOOTER_VICTORY_WAVES:
        return "清完波次";
    case SHOOTER_VICTORY_BOSS:
        return "击破首领";
    default:
        return "未知";
    }
}
