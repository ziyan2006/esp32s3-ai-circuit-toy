#include "circuit_debug.h"

#include <string.h>

#include "freertos/FreeRTOS.h"

#define DEBUG_LINK_MISS_LIMIT 2U

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static circuit_debug_state_t s_state;
static board_snapshot_t s_stable_snapshot;
static uint8_t s_stable_link_misses[BOARD_SNAPSHOT_MAX_LINKS];
static bool s_stable_snapshot_valid;

static bool snapshot_has_input(const board_snapshot_t *snapshot, uint8_t slot)
{
    return snapshot != NULL && slot < BOARD_SNAPSHOT_SLOT_COUNT &&
           snapshot->slots[slot].present && snapshot->slots[slot].id_valid &&
           snapshot->slots[slot].gate == SSD1315_GATE_INPUT;
}

void circuit_debug_init(void)
{
    portENTER_CRITICAL(&s_lock);
    memset(&s_state, 0, sizeof(s_state));
    memset(&s_stable_snapshot, 0, sizeof(s_stable_snapshot));
    memset(s_stable_link_misses, 0, sizeof(s_stable_link_misses));
    s_stable_snapshot_valid = false;
    portEXIT_CRITICAL(&s_lock);
}

void circuit_debug_reset(void)
{
    portENTER_CRITICAL(&s_lock);
    const uint32_t generation = s_state.generation + 1U;
    memset(&s_state, 0, sizeof(s_state));
    memset(&s_stable_snapshot, 0, sizeof(s_stable_snapshot));
    memset(s_stable_link_misses, 0, sizeof(s_stable_link_misses));
    s_stable_snapshot_valid = false;
    s_state.generation = generation;
    portEXIT_CRITICAL(&s_lock);
}

void circuit_debug_set_enabled(bool enabled)
{
    portENTER_CRITICAL(&s_lock);
    if (s_state.enabled != enabled) {
        s_state.enabled = enabled;
        ++s_state.generation;
    }
    portEXIT_CRITICAL(&s_lock);
}

bool circuit_debug_is_enabled(void)
{
    bool enabled;
    portENTER_CRITICAL(&s_lock);
    enabled = s_state.enabled;
    portEXIT_CRITICAL(&s_lock);
    return enabled;
}

bool circuit_debug_update_switches(uint8_t switch_mask)
{
    switch_mask &= 0x0FU;
    bool changed = false;
    portENTER_CRITICAL(&s_lock);
    if (s_state.switch_mask != switch_mask) {
        s_state.switch_mask = switch_mask;
        ++s_state.generation;
        changed = true;
    }
    portEXIT_CRITICAL(&s_lock);
    return changed;
}

uint8_t circuit_debug_compact_input_values(const circuit_debug_state_t *state)
{
    if (state == NULL) return 0U;
    uint8_t values = 0U;
    const uint8_t count = state->input_count < CIRCUIT_DEBUG_SWITCH_COUNT ?
                          state->input_count : CIRCUIT_DEBUG_SWITCH_COUNT;
    for (uint8_t index = 0; index < count; ++index) {
        const uint8_t slot = state->input_slots[index];
        if (slot < CIRCUIT_DEBUG_FIRST_INPUT_SLOT ||
            slot > CIRCUIT_DEBUG_LAST_INPUT_SLOT) continue;
        const uint8_t switch_index = CIRCUIT_DEBUG_LAST_INPUT_SLOT - slot;
        if ((state->switch_mask & (uint8_t)(1U << switch_index)) != 0U) {
            values |= (uint8_t)(1U << index);
        }
    }
    return values;
}

void circuit_debug_sync_snapshot(const board_snapshot_t *snapshot)
{
    if (snapshot == NULL || !snapshot->play_active) return;

    uint8_t current[CIRCUIT_DEBUG_SWITCH_COUNT] = {0};
    uint8_t current_count = 0;
    uint16_t misplaced_input_mask = 0U;
    for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
        if (!snapshot_has_input(snapshot, slot)) continue;
        if (slot >= CIRCUIT_DEBUG_FIRST_INPUT_SLOT &&
            slot <= CIRCUIT_DEBUG_LAST_INPUT_SLOT) {
            current[current_count++] = slot;
        } else {
            misplaced_input_mask |= (uint16_t)(1U << slot);
        }
    }

    portENTER_CRITICAL(&s_lock);
    if (current_count != s_state.input_count ||
        memcmp(current, s_state.input_slots, sizeof(current)) != 0 ||
        misplaced_input_mask != s_state.misplaced_input_mask) {
        memcpy(s_state.input_slots, current, sizeof(current));
        s_state.input_count = current_count;
        s_state.misplaced_input_mask = misplaced_input_mask;
        ++s_state.generation;
    }

    /* Keep a short-lived stable copy for debug propagation. Missing-link
     * counters advance only when a new full IR matrix arrives, not on this
     * task's 50 ms fallback polling. */
    const bool source_changed = !s_stable_snapshot_valid ||
                                snapshot->generation != s_stable_snapshot.generation;
    if (source_changed) {
        const bool advance_misses = !s_stable_snapshot_valid ||
                                    snapshot->completed_ir_scans !=
                                    s_stable_snapshot.completed_ir_scans;
        board_snapshot_t stable = *snapshot;
        memset(stable.links, 0, sizeof(stable.links));
        stable.link_count = 0;
        stable.ignored_link_count = 0;
        stable.invalid_link_count = 0;
        stable.link_overflow = snapshot->link_overflow;
        uint8_t next_misses[BOARD_SNAPSHOT_MAX_LINKS] = {0};
        bool previous_used[BOARD_SNAPSHOT_MAX_LINKS] = {0};

        for (uint8_t index = 0; index < snapshot->link_count; ++index) {
            const board_link_t *current_link = &snapshot->links[index];
            int previous_index = -1;
            if (s_stable_snapshot_valid) {
                for (uint8_t previous = 0; previous < s_stable_snapshot.link_count; ++previous) {
                    const board_link_t *old = &s_stable_snapshot.links[previous];
                    if (old->first_port == current_link->first_port &&
                        old->second_port == current_link->second_port) {
                        previous_index = previous;
                        break;
                    }
                }
            }
            if (stable.link_count >= BOARD_SNAPSHOT_MAX_LINKS) {
                stable.link_overflow = true;
                continue;
            }
            stable.links[stable.link_count] = *current_link;
            if (previous_index >= 0) previous_used[previous_index] = true;
            next_misses[stable.link_count] = 0;
            ++stable.link_count;
        }

        if (s_stable_snapshot_valid) {
            for (uint8_t previous = 0; previous < s_stable_snapshot.link_count; ++previous) {
                if (previous_used[previous]) continue;
                uint8_t misses = s_stable_link_misses[previous];
                if (advance_misses && misses < UINT8_MAX) ++misses;
                if (misses >= DEBUG_LINK_MISS_LIMIT) continue;
                if (stable.link_count >= BOARD_SNAPSHOT_MAX_LINKS) {
                    stable.link_overflow = true;
                    continue;
                }
                stable.links[stable.link_count] = s_stable_snapshot.links[previous];
                next_misses[stable.link_count] = misses;
                ++stable.link_count;
            }
        }

        for (uint8_t index = 0; index < stable.link_count; ++index) {
            if (stable.links[index].error == BOARD_LINK_UNUSED_PORT) {
                ++stable.ignored_link_count;
            } else if (!stable.links[index].valid) {
                ++stable.invalid_link_count;
            }
        }
        const bool stable_changed = !s_stable_snapshot_valid ||
                                    memcmp(stable.slots, s_stable_snapshot.slots,
                                           sizeof(stable.slots)) != 0 ||
                                    memcmp(stable.port_roles, s_stable_snapshot.port_roles,
                                           sizeof(stable.port_roles)) != 0 ||
                                    stable.link_count != s_stable_snapshot.link_count ||
                                    stable.ignored_link_count != s_stable_snapshot.ignored_link_count ||
                                    stable.invalid_link_count != s_stable_snapshot.invalid_link_count ||
                                    stable.link_overflow != s_stable_snapshot.link_overflow ||
                                    memcmp(stable.links, s_stable_snapshot.links,
                                           (size_t)stable.link_count * sizeof(stable.links[0])) != 0;
        stable.topology_revision = s_stable_snapshot_valid && !stable_changed ?
                                   s_stable_snapshot.topology_revision :
                                   s_stable_snapshot.topology_revision + 1U;
        s_stable_snapshot = stable;
        memcpy(s_stable_link_misses, next_misses, sizeof(s_stable_link_misses));
        s_stable_snapshot_valid = true;
        if (stable_changed) ++s_state.generation;
    }
    portEXIT_CRITICAL(&s_lock);
}

bool circuit_debug_get_state(circuit_debug_state_t *state)
{
    if (state == NULL) return false;
    portENTER_CRITICAL(&s_lock);
    *state = s_state;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

bool circuit_debug_get_render_snapshot(board_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    portENTER_CRITICAL(&s_lock);
    if (!s_stable_snapshot_valid) {
        portEXIT_CRITICAL(&s_lock);
        return false;
    }
    *snapshot = s_stable_snapshot;
    portEXIT_CRITICAL(&s_lock);
    return true;
}
