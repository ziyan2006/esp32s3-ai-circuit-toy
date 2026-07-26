#include "level_rules.h"

#include <stdbool.h>
#include <stddef.h>

static level_rule_t s_rules[] = {
    {101, 1, 1, 2, {0}, "让输出和输入保持一样", "A", "Y"},
    {102, 2, 1, 4, {0}, "搭出与非门的结果", "A B", "Y"},
    {103, 1, 1, 2, {0}, "让输出和输入相反", "A", "Y"},
    {201, 2, 1, 4, {0}, "两个输入都亮时输出才亮", "A B", "Y"},
    {202, 2, 1, 4, {0}, "任意输入亮时输出就亮", "A B", "Y"},
    {203, 2, 1, 4, {0}, "两个输入都不亮时输出才亮", "A B", "Y"},
    {301, 2, 1, 4, {0}, "两个输入不同时输出亮", "A B", "Y"},
    {302, 2, 1, 4, {0}, "两个输入相同时输出亮", "A B", "Y"},
    {401, 2, 1, 4, {0}, "完成二进制个位求和", "A B", "和"},
    {402, 2, 1, 4, {0}, "两个输入都亮时产生进位", "A B", "进位"},
    {403, 2, 2, 4, {0}, "同时算出求和与进位", "A B", "和 进位"},
    {501, 3, 1, 8, {0}, "输入中有奇数个亮时输出亮", "A B C", "和"},
    {502, 3, 1, 8, {0}, "至少两个输入亮时产生进位", "A B C", "进位"},
    {503, 3, 1, 8, {0}, "任意一路亮时输出就亮", "A B C", "Y"},
    {504, 3, 2, 8, {0}, "完成三输入全加运算", "A B C", "和 进位"},
    {601, 3, 1, 8, {0}, "用选择端决定输出 A 或 B", "A B 选", "Y"},
    {602, 2, 4, 4, {0}, "每种输入只点亮一个输出", "A B", "Y0 Y1 Y2 Y3"},
};

static uint8_t parity3(uint8_t value)
{
    return (uint8_t)(__builtin_popcount((unsigned)value & 0x7U) & 1U);
}

static void initialize_rule(level_rule_t *rule)
{
    for (uint8_t input = 0; input < rule->case_count; ++input) {
        const bool a = (input & 0x1U) != 0U;
        const bool b = (input & 0x2U) != 0U;
        const bool c = (input & 0x4U) != 0U;
        uint8_t output = 0;
        switch (rule->level_id) {
        case 101: output = a; break;
        case 102: output = !(a && b); break;
        case 103: output = !a; break;
        case 201: output = a && b; break;
        case 202: output = a || b; break;
        case 203: output = !(a || b); break;
        case 301:
        case 401: output = a != b; break;
        case 302: output = a == b; break;
        case 402: output = a && b; break;
        case 403: output = (uint8_t)((a != b) | ((a && b) << 1)); break;
        case 501: output = parity3(input); break;
        case 502: output = (a + b + c) >= 2; break;
        case 503: output = a || b || c; break;
        case 504:
            output = (uint8_t)(parity3(input) | (((a + b + c) >= 2) << 1));
            break;
        case 601: output = c ? b : a; break;
        case 602: output = (uint8_t)(1U << (input & 0x3U)); break;
        default: break;
        }
        rule->expected_outputs[input] = output;
    }
}

const level_rule_t *level_rule_get(uint16_t level_id)
{
    static bool initialized;
    if (!initialized) {
        for (size_t index = 0; index < sizeof(s_rules) / sizeof(s_rules[0]); ++index) {
            initialize_rule(&s_rules[index]);
        }
        initialized = true;
    }
    for (size_t index = 0; index < sizeof(s_rules) / sizeof(s_rules[0]); ++index) {
        if (s_rules[index].level_id == level_id) return &s_rules[index];
    }
    return NULL;
}
