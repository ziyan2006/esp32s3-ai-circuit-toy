#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board_snapshot.h"
#include "esp_err.h"
#include "level_rules.h"

typedef enum {
    GAME_JUDGE_IDLE = 0,
    GAME_JUDGE_RUNNING,
    GAME_JUDGE_FAILED,
    GAME_JUDGE_PASSED,
    GAME_JUDGE_CANCELLED,
    GAME_JUDGE_PRECHECK_ERROR,
} game_judge_phase_t;

typedef struct {
    uint8_t inputs;
    uint8_t expected;
    uint8_t actual;
    bool complete;
    bool passed;
} game_judge_row_t;

typedef struct {
    game_judge_phase_t phase;
    uint32_t version;
    uint32_t topology_revision;
    uint16_t level_id;
    uint8_t input_count;
    uint8_t output_count;
    uint8_t row_count;
    uint8_t completed_rows;
    uint8_t active_row;
    bool active_row_valid;
    game_judge_row_t rows[LEVEL_RULE_MAX_CASES];
    char message[64];
} game_judge_state_t;

esp_err_t game_judge_init(void);
esp_err_t game_judge_start(const level_rule_t *rule, const board_snapshot_t *snapshot);
void game_judge_cancel(const char *message);
void game_judge_reset(void);
bool game_judge_get_state(game_judge_state_t *state);
