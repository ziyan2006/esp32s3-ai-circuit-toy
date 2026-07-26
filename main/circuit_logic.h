#pragma once

#include <stdint.h>

#include "board_snapshot.h"
#include "level_rules.h"

typedef enum {
    CIRCUIT_CHECK_OK = 0,
    CIRCUIT_CHECK_UNKNOWN_LEVEL,
    CIRCUIT_CHECK_UNKNOWN_BLOCK,
    CIRCUIT_CHECK_WRONG_INPUT_COUNT,
    CIRCUIT_CHECK_WRONG_OUTPUT_COUNT,
    CIRCUIT_CHECK_INVALID_LINK,
    CIRCUIT_CHECK_INCOMPLETE,
} circuit_check_code_t;

typedef struct {
    circuit_check_code_t code;
    const char *message;
} circuit_check_result_t;

typedef struct {
    bool known[BOARD_SNAPSHOT_SLOT_COUNT];
    bool values[BOARD_SNAPSHOT_SLOT_COUNT];
} circuit_signal_state_t;

/* Propagate a concrete input assignment through the current circuit. */
bool circuit_logic_propagate(const board_snapshot_t *snapshot,
                             const uint8_t *input_slots,
                             uint8_t input_count,
                             uint8_t input_values,
                             circuit_signal_state_t *state);

/* Read the resolved signal carried by a valid output-to-input link. */
bool circuit_logic_get_link_signal(const board_snapshot_t *snapshot,
                                   const circuit_signal_state_t *state,
                                   const board_link_t *link,
                                   bool *value);

circuit_check_result_t circuit_logic_precheck(const board_snapshot_t *snapshot,
                                               const level_rule_t *rule);
circuit_check_result_t circuit_logic_evaluate(const board_snapshot_t *snapshot,
                                               const level_rule_t *rule,
                                               uint8_t input_values,
                                               uint8_t *output_values);
