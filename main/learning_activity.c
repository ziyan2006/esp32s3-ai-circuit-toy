#include "learning_activity.h"

#include <string.h>

#include "board_mapping.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "learning_activity";

static const uint8_t s_binary_targets[] = {4U, 5U, 2U};
static const uint8_t s_half_adder_patterns[] = {0x6U, 0xDU, 0xAU};
static const uint8_t s_three_input_parity_patterns[] = {0x4U, 0x6U, 0x7U};
static const uint8_t s_full_adder_patterns[] = {0x12U, 0x19U, 0x1FU};
static const uint8_t s_binary_weights[LEARNING_ACTIVITY_BINARY_COLUMN_COUNT] = {8U, 4U, 2U, 1U};

typedef struct {
    uint16_t level_id;
    learning_activity_kind_t kind;
    const uint8_t *round_values;
    uint8_t round_total;
    uint8_t slot_count;
} learning_activity_definition_t;

static const learning_activity_definition_t s_definitions[] = {
    {
        .level_id = LEARNING_ACTIVITY_BINARY_LEVEL_ID,
        .kind = LEARNING_ACTIVITY_KIND_BINARY_SLOTS,
        .round_values = s_binary_targets,
        .round_total = sizeof(s_binary_targets) / sizeof(s_binary_targets[0]),
        .slot_count = LEARNING_ACTIVITY_BINARY_COLUMN_COUNT,
    },
    {
        .level_id = LEARNING_ACTIVITY_HALF_ADDER_LEVEL_ID,
        .kind = LEARNING_ACTIVITY_KIND_HALF_ADDER,
        .round_values = s_half_adder_patterns,
        .round_total = sizeof(s_half_adder_patterns) / sizeof(s_half_adder_patterns[0]),
        .slot_count = LEARNING_ACTIVITY_BINARY_COLUMN_COUNT,
    },
    {
        .level_id = LEARNING_ACTIVITY_THREE_INPUT_PARITY_LEVEL_ID,
        .kind = LEARNING_ACTIVITY_KIND_THREE_INPUT_PARITY,
        .round_values = s_three_input_parity_patterns,
        .round_total = sizeof(s_three_input_parity_patterns) / sizeof(s_three_input_parity_patterns[0]),
        .slot_count = LEARNING_ACTIVITY_THREE_INPUT_SLOT_COUNT,
    },
    {
        .level_id = LEARNING_ACTIVITY_FULL_ADDER_LEVEL_ID,
        .kind = LEARNING_ACTIVITY_KIND_FULL_ADDER,
        .round_values = s_full_adder_patterns,
        .round_total = sizeof(s_full_adder_patterns) / sizeof(s_full_adder_patterns[0]),
        .slot_count = LEARNING_ACTIVITY_MAX_SLOT_COUNT,
    },
};

static uint8_t s_binary_slots[LEARNING_ACTIVITY_MAX_SLOT_COUNT];
static uint8_t s_half_adder_slots[LEARNING_ACTIVITY_MAX_SLOT_COUNT];
static uint8_t s_three_input_parity_slots[LEARNING_ACTIVITY_MAX_SLOT_COUNT];
static uint8_t s_full_adder_slots[LEARNING_ACTIVITY_MAX_SLOT_COUNT];
static uint8_t s_active_slots[LEARNING_ACTIVITY_MAX_SLOT_COUNT];
static bool s_activity_slots_ready;
static learning_activity_state_t s_state;

static const learning_activity_definition_t *definition_for(uint16_t level_id)
{
    for (size_t index = 0U; index < sizeof(s_definitions) / sizeof(s_definitions[0]); ++index) {
        if (s_definitions[index].level_id == level_id) return &s_definitions[index];
    }
    return NULL;
}

static bool resolve_activity_slots(void)
{
    static const uint8_t half_adder_rows[LEARNING_ACTIVITY_BINARY_COLUMN_COUNT] = {0U, 1U, 0U, 1U};
    static const uint8_t half_adder_columns[LEARNING_ACTIVITY_BINARY_COLUMN_COUNT] = {0U, 0U, 3U, 3U};
    static const uint8_t three_input_parity_rows[LEARNING_ACTIVITY_THREE_INPUT_SLOT_COUNT] = {0U, 1U, 2U};
    static const uint8_t three_input_parity_columns[LEARNING_ACTIVITY_THREE_INPUT_SLOT_COUNT] = {0U, 0U, 0U};
    static const uint8_t full_adder_rows[LEARNING_ACTIVITY_MAX_SLOT_COUNT] = {0U, 1U, 2U, 0U, 1U};
    static const uint8_t full_adder_columns[LEARNING_ACTIVITY_MAX_SLOT_COUNT] = {0U, 0U, 0U, 3U, 3U};
    bool binary_found[LEARNING_ACTIVITY_BINARY_COLUMN_COUNT] = {0};
    bool half_adder_found[LEARNING_ACTIVITY_BINARY_COLUMN_COUNT] = {0};
    bool three_input_parity_found[LEARNING_ACTIVITY_THREE_INPUT_SLOT_COUNT] = {0};
    bool full_adder_found[LEARNING_ACTIVITY_MAX_SLOT_COUNT] = {0};
    memset(s_binary_slots, UINT8_MAX, sizeof(s_binary_slots));
    memset(s_half_adder_slots, UINT8_MAX, sizeof(s_half_adder_slots));
    memset(s_three_input_parity_slots, UINT8_MAX, sizeof(s_three_input_parity_slots));
    memset(s_full_adder_slots, UINT8_MAX, sizeof(s_full_adder_slots));

    for (uint8_t slot = 0U; slot < BOARD_MAPPING_SLOT_COUNT; ++slot) {
        const board_slot_mapping_t *mapping = board_mapping_slot(slot);
        if (mapping == NULL) continue;
        if (mapping->grid_row == 0U &&
            mapping->grid_column < LEARNING_ACTIVITY_BINARY_COLUMN_COUNT &&
            !binary_found[mapping->grid_column]) {
            s_binary_slots[mapping->grid_column] = slot;
            binary_found[mapping->grid_column] = true;
        }
        for (uint8_t column = 0U; column < LEARNING_ACTIVITY_BINARY_COLUMN_COUNT; ++column) {
            if (mapping->grid_row == half_adder_rows[column] &&
                mapping->grid_column == half_adder_columns[column] &&
                !half_adder_found[column]) {
                s_half_adder_slots[column] = slot;
                half_adder_found[column] = true;
            }
        }
        for (uint8_t column = 0U; column < LEARNING_ACTIVITY_THREE_INPUT_SLOT_COUNT; ++column) {
            if (mapping->grid_row == three_input_parity_rows[column] &&
                mapping->grid_column == three_input_parity_columns[column] &&
                !three_input_parity_found[column]) {
                s_three_input_parity_slots[column] = slot;
                three_input_parity_found[column] = true;
            }
        }
        for (uint8_t column = 0U; column < LEARNING_ACTIVITY_MAX_SLOT_COUNT; ++column) {
            if (mapping->grid_row == full_adder_rows[column] &&
                mapping->grid_column == full_adder_columns[column] &&
                !full_adder_found[column]) {
                s_full_adder_slots[column] = slot;
                full_adder_found[column] = true;
            }
        }
    }

    for (uint8_t column = 0U; column < LEARNING_ACTIVITY_BINARY_COLUMN_COUNT; ++column) {
        if (!binary_found[column] || !half_adder_found[column]) return false;
    }
    for (uint8_t column = 0U; column < LEARNING_ACTIVITY_THREE_INPUT_SLOT_COUNT; ++column) {
        if (!three_input_parity_found[column]) return false;
    }
    for (uint8_t column = 0U; column < LEARNING_ACTIVITY_MAX_SLOT_COUNT; ++column) {
        if (!full_adder_found[column]) return false;
    }
    s_activity_slots_ready = true;
    return true;
}

static void apply_round_definition(const learning_activity_definition_t *definition)
{
    const uint8_t value = definition->round_values[s_state.round_index];
    s_state.target_decimal = definition->kind == LEARNING_ACTIVITY_KIND_BINARY_SLOTS ? value : 0U;
    s_state.target_bits = definition->kind == LEARNING_ACTIVITY_KIND_BINARY_SLOTS ? 0U : value;
    if (definition->kind == LEARNING_ACTIVITY_KIND_THREE_INPUT_PARITY) {
        s_state.target_decimal = (uint8_t)(__builtin_popcount((unsigned)value) & 1U);
    }
}

static void refresh_solution_state(void)
{
    const bool solved = s_state.kind == LEARNING_ACTIVITY_KIND_BINARY_SLOTS ?
        s_state.current_decimal == s_state.target_decimal :
        s_state.kind == LEARNING_ACTIVITY_KIND_THREE_INPUT_PARITY ?
            (s_state.current_decimal & 1U) == s_state.target_decimal :
            s_state.bits == s_state.target_bits;
    const bool complete = solved && s_state.round_index + 1U >= s_state.round_total;
    if (s_state.solved != solved || s_state.complete != complete) {
        s_state.solved = solved;
        s_state.complete = complete;
        ++s_state.generation;
    }
}

bool learning_activity_is_available(uint16_t level_id)
{
    return definition_for(level_id) != NULL;
}

bool learning_activity_begin(uint16_t level_id)
{
    const learning_activity_definition_t *definition = definition_for(level_id);
    if (definition == NULL || (!s_activity_slots_ready && !resolve_activity_slots())) return false;

    const uint8_t *slots = definition->kind == LEARNING_ACTIVITY_KIND_HALF_ADDER ?
        s_half_adder_slots : definition->kind == LEARNING_ACTIVITY_KIND_THREE_INPUT_PARITY ?
            s_three_input_parity_slots : definition->kind == LEARNING_ACTIVITY_KIND_FULL_ADDER ?
                s_full_adder_slots : s_binary_slots;
    memcpy(s_active_slots, slots, sizeof(s_active_slots));

    s_state = (learning_activity_state_t) {
        .active = true,
        .level_id = level_id,
        .kind = definition->kind,
        .slot_count = definition->slot_count,
        .round_total = definition->round_total,
        .generation = s_state.generation + 1U,
    };
    apply_round_definition(definition);
    ESP_LOGI(TAG, "started level=%u kind=%u slots=%u/%u/%u/%u/%u",
             level_id, definition->kind, s_active_slots[0], s_active_slots[1],
             s_active_slots[2], s_active_slots[3], s_active_slots[4]);
    return true;
}

void learning_activity_stop(void)
{
    if (!s_state.active) return;
    s_state.active = false;
    ++s_state.generation;
    ESP_LOGI(TAG, "stopped");
}

bool learning_activity_is_active(void)
{
    return s_state.active;
}

void learning_activity_update_snapshot(const board_snapshot_t *snapshot)
{
    if (!s_state.active || snapshot == NULL) return;

    uint8_t bits = 0U;
    uint8_t current_decimal = 0U;
    for (uint8_t column = 0U; column < s_state.slot_count; ++column) {
        const uint8_t slot = s_active_slots[column];
        if (slot >= BOARD_SNAPSHOT_SLOT_COUNT || !snapshot->slots[slot].present) continue;
        bits |= (uint8_t)(1U << (s_state.slot_count - 1U - column));
        if (s_state.kind == LEARNING_ACTIVITY_KIND_BINARY_SLOTS) {
            current_decimal = (uint8_t)(current_decimal + s_binary_weights[column]);
        } else if (s_state.kind == LEARNING_ACTIVITY_KIND_THREE_INPUT_PARITY) {
            ++current_decimal;
        }
    }

    if (s_state.bits != bits || s_state.current_decimal != current_decimal) {
        s_state.bits = bits;
        s_state.current_decimal = current_decimal;
        ++s_state.generation;
    }
    refresh_solution_state();
}

bool learning_activity_advance(void)
{
    if (!s_state.active || !s_state.solved || s_state.complete) return false;
    const learning_activity_definition_t *definition = definition_for(s_state.level_id);
    if (definition == NULL || s_state.round_index + 1U >= definition->round_total) return false;
    ++s_state.round_index;
    apply_round_definition(definition);
    s_state.solved = false;
    s_state.complete = false;
    ++s_state.generation;
    refresh_solution_state();
    ESP_LOGI(TAG, "advanced level=%u kind=%u round=%u",
             s_state.level_id, definition->kind, s_state.round_index + 1U);
    return true;
}

bool learning_activity_is_complete(void)
{
    return s_state.active && s_state.complete;
}

bool learning_activity_get_state(learning_activity_state_t *state)
{
    if (state == NULL || !s_state.active) return false;
    *state = s_state;
    return true;
}

uint8_t learning_activity_slot_for_column(uint8_t column)
{
    if (column >= LEARNING_ACTIVITY_MAX_SLOT_COUNT || column >= s_state.slot_count ||
        (!s_activity_slots_ready && !resolve_activity_slots())) {
        return UINT8_MAX;
    }
    return s_active_slots[column];
}

esp_err_t learning_activity_self_test_run(void)
{
    board_snapshot_t snapshot = {0};
    learning_activity_state_t state;

    ESP_RETURN_ON_FALSE(learning_activity_is_available(LEARNING_ACTIVITY_HALF_ADDER_LEVEL_ID),
                        ESP_FAIL, TAG, "half adder activity should be available");
    ESP_RETURN_ON_FALSE(learning_activity_is_available(LEARNING_ACTIVITY_THREE_INPUT_PARITY_LEVEL_ID),
                        ESP_FAIL, TAG, "three input parity activity should be available");
    ESP_RETURN_ON_FALSE(learning_activity_is_available(LEARNING_ACTIVITY_FULL_ADDER_LEVEL_ID),
                        ESP_FAIL, TAG, "full adder activity should be available");
    ESP_RETURN_ON_FALSE(!learning_activity_is_available(402U), ESP_FAIL, TAG,
                        "unexpected activity level");
    ESP_RETURN_ON_FALSE(learning_activity_begin(LEARNING_ACTIVITY_BINARY_LEVEL_ID), ESP_FAIL, TAG,
                        "binary activity start failed");
    ESP_RETURN_ON_FALSE(learning_activity_slot_for_column(0U) == 6U &&
                            learning_activity_slot_for_column(1U) == 4U &&
                            learning_activity_slot_for_column(2U) == 2U &&
                            learning_activity_slot_for_column(3U) == 0U,
                        ESP_FAIL, TAG, "binary slot mapping mismatch");

    snapshot.slots[4U].present = true;
    learning_activity_update_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(learning_activity_get_state(&state) && state.bits == 0x4U &&
                            state.current_decimal == 4U && state.solved,
                        ESP_FAIL, TAG, "binary 0100 should solve target 4");
    ESP_RETURN_ON_FALSE(learning_activity_advance(), ESP_FAIL, TAG,
                        "advance after solved first round failed");

    snapshot.slots[0U].present = true;
    learning_activity_update_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(learning_activity_get_state(&state) && state.bits == 0x5U &&
                            state.current_decimal == 5U && state.solved,
                        ESP_FAIL, TAG, "binary 0101 should solve target 5");
    ESP_RETURN_ON_FALSE(learning_activity_advance(), ESP_FAIL, TAG,
                        "advance after solved second round failed");

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.slots[2U].present = true;
    learning_activity_update_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(learning_activity_get_state(&state) && state.bits == 0x2U &&
                            state.current_decimal == 2U && state.complete,
                        ESP_FAIL, TAG, "binary 0010 should complete target 2");
    learning_activity_stop();

    ESP_RETURN_ON_FALSE(learning_activity_begin(LEARNING_ACTIVITY_THREE_INPUT_PARITY_LEVEL_ID), ESP_FAIL,
                        TAG, "three input parity activity start failed");
    ESP_RETURN_ON_FALSE(learning_activity_get_state(&state) &&
                            state.kind == LEARNING_ACTIVITY_KIND_THREE_INPUT_PARITY &&
                            state.slot_count == LEARNING_ACTIVITY_THREE_INPUT_SLOT_COUNT &&
                            state.target_bits == 0x4U && state.target_decimal == 1U,
                        ESP_FAIL, TAG, "three input parity first pattern mismatch");
    ESP_RETURN_ON_FALSE(learning_activity_slot_for_column(0U) == 6U &&
                            learning_activity_slot_for_column(1U) == 7U &&
                            learning_activity_slot_for_column(2U) == 8U,
                        ESP_FAIL, TAG, "three input parity slot mapping mismatch");

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.slots[6U].present = true;
    learning_activity_update_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(learning_activity_get_state(&state) && state.bits == 0x4U &&
                            state.current_decimal == 1U && state.solved,
                        ESP_FAIL, TAG, "one input should produce an odd parity result");
    ESP_RETURN_ON_FALSE(learning_activity_advance(), ESP_FAIL, TAG,
                        "three input parity first advance failed");

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.slots[6U].present = true;
    snapshot.slots[7U].present = true;
    learning_activity_update_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(learning_activity_get_state(&state) && state.bits == 0x6U &&
                            state.current_decimal == 2U && state.solved,
                        ESP_FAIL, TAG, "two inputs should produce an even parity result");
    ESP_RETURN_ON_FALSE(learning_activity_advance(), ESP_FAIL, TAG,
                        "three input parity second advance failed");

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.slots[6U].present = true;
    snapshot.slots[7U].present = true;
    snapshot.slots[8U].present = true;
    learning_activity_update_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(learning_activity_get_state(&state) && state.bits == 0x7U &&
                            state.current_decimal == 3U && state.complete,
                        ESP_FAIL, TAG, "three inputs should complete odd parity training");
    learning_activity_stop();

    ESP_RETURN_ON_FALSE(learning_activity_begin(LEARNING_ACTIVITY_HALF_ADDER_LEVEL_ID), ESP_FAIL,
                        TAG, "half adder activity start failed");
    ESP_RETURN_ON_FALSE(learning_activity_get_state(&state) &&
                            state.kind == LEARNING_ACTIVITY_KIND_HALF_ADDER &&
                            state.target_bits == 0x6U,
                        ESP_FAIL, TAG, "half adder first pattern mismatch");
    ESP_RETURN_ON_FALSE(learning_activity_slot_for_column(0U) == 6U &&
                            learning_activity_slot_for_column(1U) == 7U &&
                            learning_activity_slot_for_column(2U) == 0U &&
                            learning_activity_slot_for_column(3U) == 1U,
                        ESP_FAIL, TAG, "half adder slot mapping mismatch");

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.slots[7U].present = true;
    snapshot.slots[0U].present = true;
    learning_activity_update_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(learning_activity_get_state(&state) && state.bits == 0x6U && state.solved,
                        ESP_FAIL, TAG, "half adder 0 plus 1 pattern mismatch");
    ESP_RETURN_ON_FALSE(learning_activity_advance(), ESP_FAIL, TAG,
                        "half adder first advance failed");

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.slots[6U].present = true;
    snapshot.slots[7U].present = true;
    snapshot.slots[1U].present = true;
    learning_activity_update_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(learning_activity_get_state(&state) && state.bits == 0xDU && state.solved,
                        ESP_FAIL, TAG, "half adder 1 plus 1 pattern mismatch");
    ESP_RETURN_ON_FALSE(learning_activity_advance(), ESP_FAIL, TAG,
                        "half adder second advance failed");

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.slots[6U].present = true;
    snapshot.slots[0U].present = true;
    learning_activity_update_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(learning_activity_get_state(&state) && state.bits == 0xAU &&
                            state.complete,
                        ESP_FAIL, TAG, "half adder 1 plus 0 pattern mismatch");
    learning_activity_stop();

    ESP_RETURN_ON_FALSE(learning_activity_begin(LEARNING_ACTIVITY_FULL_ADDER_LEVEL_ID), ESP_FAIL,
                        TAG, "full adder activity start failed");
    ESP_RETURN_ON_FALSE(learning_activity_get_state(&state) &&
                            state.kind == LEARNING_ACTIVITY_KIND_FULL_ADDER &&
                            state.slot_count == LEARNING_ACTIVITY_MAX_SLOT_COUNT &&
                            state.target_bits == 0x12U,
                        ESP_FAIL, TAG, "full adder first pattern mismatch");
    ESP_RETURN_ON_FALSE(learning_activity_slot_for_column(0U) == 6U &&
                            learning_activity_slot_for_column(1U) == 7U &&
                            learning_activity_slot_for_column(2U) == 8U &&
                            learning_activity_slot_for_column(3U) == 0U &&
                            learning_activity_slot_for_column(4U) == 1U,
                        ESP_FAIL, TAG, "full adder slot mapping mismatch");

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.slots[6U].present = true;
    snapshot.slots[0U].present = true;
    learning_activity_update_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(learning_activity_get_state(&state) && state.bits == 0x12U && state.solved,
                        ESP_FAIL, TAG, "full adder 1 plus 0 plus 0 pattern mismatch");
    ESP_RETURN_ON_FALSE(learning_activity_advance(), ESP_FAIL, TAG,
                        "full adder first advance failed");

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.slots[6U].present = true;
    snapshot.slots[7U].present = true;
    snapshot.slots[1U].present = true;
    learning_activity_update_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(learning_activity_get_state(&state) && state.bits == 0x19U && state.solved,
                        ESP_FAIL, TAG, "full adder 1 plus 1 plus 0 pattern mismatch");
    ESP_RETURN_ON_FALSE(learning_activity_advance(), ESP_FAIL, TAG,
                        "full adder second advance failed");

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.slots[6U].present = true;
    snapshot.slots[7U].present = true;
    snapshot.slots[8U].present = true;
    snapshot.slots[0U].present = true;
    snapshot.slots[1U].present = true;
    learning_activity_update_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(learning_activity_get_state(&state) && state.bits == 0x1FU &&
                            state.complete,
                        ESP_FAIL, TAG, "full adder 1 plus 1 plus 1 pattern mismatch");
    learning_activity_stop();
    ESP_LOGI(TAG, "binary, half adder, three input parity, and full adder activity self-tests passed");
    return ESP_OK;
}
