#include "shooter_wave.h"

static const shooter_wave_segment_t s_stage1_segments[] = {
    {SHOOTER_WAVE_TIER_NORMAL, "首次接触", 5200U, 980U, 5U, 0U, SHOOTER_ENEMY_ARCHETYPE_GRUNT, true},
    {SHOOTER_WAVE_TIER_PRESSURE, "门阵扫荡", 6400U, 820U, 7U, 3U, SHOOTER_ENEMY_ARCHETYPE_LASER_DRONE, true},
    {SHOOTER_WAVE_TIER_BREATHER, "冷却窗口", 2600U, 0U, 0U, 0U, SHOOTER_ENEMY_ARCHETYPE_GRUNT, false},
    {SHOOTER_WAVE_TIER_PRESSURE, "锁存训练", 7000U, 760U, 8U, 0U, SHOOTER_ENEMY_ARCHETYPE_GRUNT, true},
};

static const shooter_wave_segment_t s_stage2_segments[] = {
    {SHOOTER_WAVE_TIER_NORMAL, "位流漂移", 5600U, 900U, 6U, 0U, SHOOTER_ENEMY_ARCHETYPE_GRUNT, true},
    {SHOOTER_WAVE_TIER_BREATHER, "复位间隙", 2200U, 0U, 0U, 0U, SHOOTER_ENEMY_ARCHETYPE_GRUNT, false},
    {SHOOTER_WAVE_TIER_PRESSURE, "首次俯冲", 6800U, 720U, 8U, 4U, SHOOTER_ENEMY_ARCHETYPE_ELITE_DASHER, true},
    {SHOOTER_WAVE_TIER_PRESSURE, "进位突袭", 7200U, 660U, 9U, 3U, SHOOTER_ENEMY_ARCHETYPE_ELITE_DASHER, true},
};

static const shooter_wave_segment_t s_stage3_segments[] = {
    {SHOOTER_WAVE_TIER_NORMAL, "门阵巡逻", 5400U, 820U, 7U, 0U, SHOOTER_ENEMY_ARCHETYPE_GRUNT, true},
    {SHOOTER_WAVE_TIER_PRESSURE, "追踪唤醒", 6400U, 700U, 9U, 3U, SHOOTER_ENEMY_ARCHETYPE_ELITE_BRUISER, true},
    {SHOOTER_WAVE_TIER_BREATHER, "稳定窗口", 2200U, 0U, 0U, 0U, SHOOTER_ENEMY_ARCHETYPE_GRUNT, false},
    {SHOOTER_WAVE_TIER_PRESSURE, "与门压制", 7200U, 640U, 10U, 3U, SHOOTER_ENEMY_ARCHETYPE_ELITE_BRUISER, true},
};

static const shooter_wave_segment_t s_stage4_segments[] = {
    {SHOOTER_WAVE_TIER_NORMAL, "轨迹线", 5200U, 760U, 7U, 0U, SHOOTER_ENEMY_ARCHETYPE_GRUNT, true},
    {SHOOTER_WAVE_TIER_PRESSURE, "奇偶风暴", 6600U, 640U, 10U, 4U, SHOOTER_ENEMY_ARCHETYPE_ELITE_DASHER, true},
    {SHOOTER_WAVE_TIER_BREATHER, "奇偶间歇", 2400U, 0U, 0U, 0U, SHOOTER_ENEMY_ARCHETYPE_GRUNT, false},
    {SHOOTER_WAVE_TIER_PRESSURE, "夹击堆叠", 7800U, 600U, 11U, 3U, SHOOTER_ENEMY_ARCHETYPE_ELITE_PINNER, true},
};

static const shooter_wave_segment_t s_stage5_segments[] = {
    {SHOOTER_WAVE_TIER_NORMAL, "反应堆供能", 5400U, 740U, 7U, 0U, SHOOTER_ENEMY_ARCHETYPE_GRUNT, true},
    {SHOOTER_WAVE_TIER_PRESSURE, "异或共振", 6600U, 600U, 10U, 3U, SHOOTER_ENEMY_ARCHETYPE_ELITE_DASHER, true},
    {SHOOTER_WAVE_TIER_BREATHER, "封控间隙", 2400U, 0U, 0U, 0U, SHOOTER_ENEMY_ARCHETYPE_GRUNT, false},
    {SHOOTER_WAVE_TIER_PRESSURE, "断层钻击", 7000U, 560U, 11U, 3U, SHOOTER_ENEMY_ARCHETYPE_ELITE_BRUISER, true},
    {SHOOTER_WAVE_TIER_BOSS, "异或反应堆", 0U, 0U, 0U, 0U, SHOOTER_ENEMY_ARCHETYPE_BOSS, true},
};

static const shooter_wave_segment_t s_stage6_segments[] = {
    {SHOOTER_WAVE_TIER_NORMAL, "外墙防线", 5600U, 700U, 8U, 0U, SHOOTER_ENEMY_ARCHETYPE_GRUNT, true},
    {SHOOTER_WAVE_TIER_PRESSURE, "堡垒输出", 7000U, 560U, 11U, 3U, SHOOTER_ENEMY_ARCHETYPE_ELITE_BRUISER, true},
    {SHOOTER_WAVE_TIER_BREATHER, "护盾同步", 2200U, 0U, 0U, 0U, SHOOTER_ENEMY_ARCHETYPE_GRUNT, false},
    {SHOOTER_WAVE_TIER_PRESSURE, "锁存走廊", 7600U, 520U, 13U, 2U, SHOOTER_ENEMY_ARCHETYPE_ELITE_PINNER, true},
    {SHOOTER_WAVE_TIER_BOSS, "与门要塞", 0U, 0U, 0U, 0U, SHOOTER_ENEMY_ARCHETYPE_BOSS, true},
};

static const shooter_wave_plan_t s_plans[] = {
    {"非门熔炉", 4U, s_stage1_segments},
    {"加法器航道", 4U, s_stage2_segments},
    {"与门堡垒", 4U, s_stage3_segments},
    {"奇偶回路", 4U, s_stage4_segments},
    {"异或反应堆", 5U, s_stage5_segments},
    {"与门要塞", 5U, s_stage6_segments},
};

const shooter_wave_plan_t *shooter_wave_get_plan(uint32_t level_id)
{
    if (level_id < 1U || level_id > (uint32_t)(sizeof(s_plans) / sizeof(s_plans[0]))) {
        level_id = 1U;
    }

    return &s_plans[level_id - 1U];
}

const char *shooter_wave_tier_name(shooter_wave_tier_t tier)
{
    switch (tier) {
    case SHOOTER_WAVE_TIER_NORMAL:
        return "普通";
    case SHOOTER_WAVE_TIER_PRESSURE:
        return "压制";
    case SHOOTER_WAVE_TIER_BREATHER:
        return "喘息";
    case SHOOTER_WAVE_TIER_BOSS:
        return "首领";
    default:
        return "未知";
    }
}
