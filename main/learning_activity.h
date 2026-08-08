#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board_snapshot.h"
#include "esp_err.h"

#define LEARNING_ACTIVITY_BINARY_LEVEL_ID 401U
#define LEARNING_ACTIVITY_HALF_ADDER_LEVEL_ID 403U
#define LEARNING_ACTIVITY_THREE_INPUT_PARITY_LEVEL_ID 501U
#define LEARNING_ACTIVITY_THREE_INPUT_CARRY_LEVEL_ID 502U
#define LEARNING_ACTIVITY_FULL_ADDER_LEVEL_ID 503U
#define LEARNING_ACTIVITY_BINARY_COLUMN_COUNT 4U
#define LEARNING_ACTIVITY_THREE_INPUT_SLOT_COUNT 3U
#define LEARNING_ACTIVITY_MAX_SLOT_COUNT 5U

typedef enum {
    LEARNING_ACTIVITY_KIND_BINARY_SLOTS,
    LEARNING_ACTIVITY_KIND_HALF_ADDER,
    LEARNING_ACTIVITY_KIND_THREE_INPUT_PARITY,
    LEARNING_ACTIVITY_KIND_THREE_INPUT_CARRY,
    LEARNING_ACTIVITY_KIND_FULL_ADDER,
} learning_activity_kind_t;

typedef struct {
    bool active;
    uint16_t level_id;
    learning_activity_kind_t kind;
    uint8_t slot_count;
    uint8_t round_index;
    uint8_t round_total;
    uint8_t target_decimal;
    uint8_t target_bits;
    uint8_t current_decimal;
    uint8_t bits;
    bool solved;
    bool complete;
    uint32_t generation;
} learning_activity_state_t;

bool learning_activity_is_available(uint16_t level_id);
bool learning_activity_begin(uint16_t level_id);
void learning_activity_stop(void);
bool learning_activity_is_active(void);
void learning_activity_update_snapshot(const board_snapshot_t *snapshot);
bool learning_activity_advance(void);
bool learning_activity_is_complete(void);
bool learning_activity_get_state(learning_activity_state_t *state);
uint8_t learning_activity_slot_for_column(uint8_t column);
esp_err_t learning_activity_self_test_run(void);
