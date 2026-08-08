#include "circuit_role_labels.h"

#include "level_rules.h"

#include <string.h>

typedef struct {
    uint16_t level_id;
    uint8_t input_count;
    uint8_t output_count;
    const char *input_labels[LEVEL_RULE_MAX_INPUTS];
    const char *output_labels[LEVEL_RULE_MAX_OUTPUTS];
} role_label_rule_t;

static const role_label_rule_t s_role_label_rules[] = {
    {401U, 2U, 1U, {"A", "B"}, {"个"}},
    {402U, 2U, 1U, {"A", "B"}, {"进"}},
    {403U, 2U, 2U, {"A", "B"}, {"个", "进"}},
    {501U, 3U, 1U, {"A", "B", "C"}, {"个"}},
    {502U, 3U, 1U, {"A", "B", "C"}, {"进"}},
    {503U, 3U, 2U, {"A", "B", "C"}, {"个", "进"}},
    {601U, 3U, 1U, {"A", "B", "选"}, {"Y"}},
    {602U, 2U, 4U, {"A", "B"}, {"0", "1", "2", "3"}},
};

static const role_label_rule_t *find_rule(uint16_t level_id)
{
    for (size_t index = 0; index < sizeof(s_role_label_rules) / sizeof(s_role_label_rules[0]); ++index) {
        if (s_role_label_rules[index].level_id == level_id) return &s_role_label_rules[index];
    }
    return NULL;
}

static void clear_role_labels(board_slot_identity_t *slots)
{
    for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
        slots[slot].role_label[0] = '\0';
    }
}

void circuit_role_labels_assign(uint16_t level_id, board_slot_identity_t *slots)
{
    if (slots == NULL) return;
    clear_role_labels(slots);

    const role_label_rule_t *rule = find_rule(level_id);
    if (rule == NULL) return;

    uint8_t input_count = 0;
    uint8_t output_count = 0;
    for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
        if (!slots[slot].present || !slots[slot].id_valid) continue;
        if (slots[slot].gate == SSD1315_GATE_INPUT) {
            ++input_count;
        } else if (slots[slot].gate == SSD1315_GATE_OUTPUT) {
            ++output_count;
        }
    }
    if (input_count != rule->input_count || output_count != rule->output_count) return;

    input_count = 0;
    output_count = 0;
    for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
        if (!slots[slot].present || !slots[slot].id_valid) continue;
        const char *label = NULL;
        if (slots[slot].gate == SSD1315_GATE_INPUT) {
            label = rule->input_labels[input_count++];
        } else if (slots[slot].gate == SSD1315_GATE_OUTPUT) {
            label = rule->output_labels[output_count++];
        }
        if (label != NULL) {
            strncpy(slots[slot].role_label, label, BOARD_ROLE_LABEL_MAX - 1U);
            slots[slot].role_label[BOARD_ROLE_LABEL_MAX - 1U] = '\0';
        }
    }
}
