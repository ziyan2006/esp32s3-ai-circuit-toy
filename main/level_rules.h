#pragma once

#include <stdint.h>

#define LEVEL_RULE_MAX_INPUTS   4U
#define LEVEL_RULE_MAX_OUTPUTS  4U
#define LEVEL_RULE_MAX_CASES    16U

typedef struct {
    uint16_t level_id;
    uint16_t rule_version;
    uint8_t input_count;
    uint8_t output_count;
    uint8_t case_count;
    uint8_t expected_outputs[LEVEL_RULE_MAX_CASES];
    const char *short_goal;
    const char *input_names;
    const char *output_names;
} level_rule_t;

const level_rule_t *level_rule_get(uint16_t level_id);
