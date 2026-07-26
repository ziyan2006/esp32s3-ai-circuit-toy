#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board_snapshot.h"

#define CIRCUIT_DEBUG_SWITCH_COUNT       4U
#define CIRCUIT_DEBUG_FIRST_INPUT_SLOT   6U  /* physical slot 7 */
#define CIRCUIT_DEBUG_LAST_INPUT_SLOT    9U  /* physical slot 10 */

typedef struct {
    bool enabled;
    uint32_t generation;
    uint8_t switch_mask;
    /* Valid input blocks in physical slots 7..10, ordered by slot. */
    uint8_t input_slots[CIRCUIT_DEBUG_SWITCH_COUNT];
    uint8_t input_count;
    /* Bit N is set when an input block is present outside physical slots 7..10. */
    uint16_t misplaced_input_mask;
} circuit_debug_state_t;

void circuit_debug_init(void);
void circuit_debug_reset(void);
void circuit_debug_set_enabled(bool enabled);
bool circuit_debug_is_enabled(void);
bool circuit_debug_update_switches(uint8_t switch_mask);
uint8_t circuit_debug_compact_input_values(const circuit_debug_state_t *state);
void circuit_debug_sync_snapshot(const board_snapshot_t *snapshot);
bool circuit_debug_get_state(circuit_debug_state_t *state);
bool circuit_debug_get_render_snapshot(board_snapshot_t *snapshot);
