#include "level_rules.h"

#include <stddef.h>

static const level_rule_t s_rules[] = {
#include "level_rules.generated.inc"
};

const level_rule_t *level_rule_get(uint16_t level_id)
{
    for (size_t index = 0; index < sizeof(s_rules) / sizeof(s_rules[0]); ++index) {
        if (s_rules[index].level_id == level_id) return &s_rules[index];
    }
    return NULL;
}
