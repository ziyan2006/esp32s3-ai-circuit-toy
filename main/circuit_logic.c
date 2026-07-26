#include "circuit_logic.h"

#include <stdbool.h>
#include <string.h>

static circuit_check_result_t result(circuit_check_code_t code, const char *message)
{
    return (circuit_check_result_t) {.code = code, .message = message};
}

static bool find_source_port(const board_snapshot_t *snapshot,
                             uint8_t input_port,
                             uint8_t *source_port)
{
    for (uint8_t index = 0; index < snapshot->link_count; ++index) {
        const board_link_t *link = &snapshot->links[index];
        if (!link->valid) continue;
        if (link->first_port == input_port &&
            snapshot->port_roles[link->second_port] == BOARD_PORT_OUTPUT) {
            *source_port = link->second_port;
            return true;
        }
        if (link->second_port == input_port &&
            snapshot->port_roles[link->first_port] == BOARD_PORT_OUTPUT) {
            *source_port = link->first_port;
            return true;
        }
    }
    return false;
}

static bool read_gate_input(const board_snapshot_t *snapshot,
                            uint8_t slot,
                            uint8_t input_index,
                            const bool *known,
                            const bool *values,
                            bool *value)
{
    uint8_t input_port = UINT8_MAX;
    uint8_t seen = 0;
    for (uint8_t local = 0; local < BOARD_SNAPSHOT_PORTS_PER_SLOT; ++local) {
        const uint8_t port = slot * BOARD_SNAPSHOT_PORTS_PER_SLOT + local;
        if (snapshot->port_roles[port] != BOARD_PORT_INPUT) continue;
        if (seen++ == input_index) {
            input_port = port;
            break;
        }
    }
    if (input_port == UINT8_MAX) return false;
    uint8_t source_port;
    if (!find_source_port(snapshot, input_port, &source_port)) return false;
    const uint8_t source_slot = board_mapping_slot_for_port(source_port);
    if (!known[source_slot]) return false;
    *value = values[source_slot];
    return true;
}

bool circuit_logic_propagate(const board_snapshot_t *snapshot,
                             const uint8_t *input_slots,
                             uint8_t input_count,
                             uint8_t input_values,
                             circuit_signal_state_t *state)
{
    if (snapshot == NULL || state == NULL ||
        (input_count > 0U && input_slots == NULL) ||
        input_count > BOARD_SNAPSHOT_SLOT_COUNT) {
        return false;
    }

    memset(state, 0, sizeof(*state));
    for (uint8_t input = 0; input < input_count; ++input) {
        const uint8_t slot = input_slots[input];
        if (slot >= BOARD_SNAPSHOT_SLOT_COUNT || !snapshot->slots[slot].present ||
            !snapshot->slots[slot].id_valid || snapshot->slots[slot].gate != SSD1315_GATE_INPUT) {
            return false;
        }
        state->known[slot] = true;
        state->values[slot] = (input_values & (1U << input)) != 0U;
    }

    for (uint8_t pass = 0; pass < BOARD_SNAPSHOT_SLOT_COUNT; ++pass) {
        bool progress = false;
        for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
            if (!snapshot->slots[slot].present || !snapshot->slots[slot].id_valid ||
                state->known[slot]) continue;
            const ssd1315_gate_t gate = snapshot->slots[slot].gate;
            if (gate == SSD1315_GATE_INPUT || gate == SSD1315_GATE_OUTPUT ||
                gate == SSD1315_GATE_NULL) continue;

            bool first;
            bool second;
            if (!read_gate_input(snapshot, slot, 0, state->known, state->values, &first)) continue;
            if (gate == SSD1315_GATE_NOT) {
                state->values[slot] = !first;
            } else {
                if (!read_gate_input(snapshot, slot, 1, state->known, state->values, &second)) continue;
                switch (gate) {
                case SSD1315_GATE_AND: state->values[slot] = first && second; break;
                case SSD1315_GATE_OR: state->values[slot] = first || second; break;
                case SSD1315_GATE_NAND: state->values[slot] = !(first && second); break;
                case SSD1315_GATE_NOR: state->values[slot] = !(first || second); break;
                case SSD1315_GATE_XOR: state->values[slot] = first != second; break;
                case SSD1315_GATE_XNOR: state->values[slot] = first == second; break;
                default: continue;
                }
            }
            state->known[slot] = true;
            progress = true;
        }
        if (!progress) break;
    }

    /* Output blocks are sinks, but exposing their resolved value lets the
     * debug renderer light all four physical output ports consistently. */
    for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
        if (!snapshot->slots[slot].present || !snapshot->slots[slot].id_valid ||
            snapshot->slots[slot].gate != SSD1315_GATE_OUTPUT) continue;
        bool value;
        if (read_gate_input(snapshot, slot, 0, state->known, state->values, &value)) {
            state->known[slot] = true;
            state->values[slot] = value;
        }
    }
    return true;
}

bool circuit_logic_get_link_signal(const board_snapshot_t *snapshot,
                                   const circuit_signal_state_t *state,
                                   const board_link_t *link,
                                   bool *value)
{
    if (snapshot == NULL || state == NULL || link == NULL || value == NULL || !link->valid ||
        link->first_port >= BOARD_SNAPSHOT_PORT_COUNT || link->second_port >= BOARD_SNAPSHOT_PORT_COUNT) {
        return false;
    }
    uint8_t source_port = UINT8_MAX;
    if (snapshot->port_roles[link->first_port] == BOARD_PORT_OUTPUT &&
        snapshot->port_roles[link->second_port] == BOARD_PORT_INPUT) {
        source_port = link->first_port;
    } else if (snapshot->port_roles[link->second_port] == BOARD_PORT_OUTPUT &&
               snapshot->port_roles[link->first_port] == BOARD_PORT_INPUT) {
        source_port = link->second_port;
    }
    if (source_port == UINT8_MAX) return false;
    const uint8_t source_slot = board_mapping_slot_for_port(source_port);
    if (!state->known[source_slot]) return false;
    *value = state->values[source_slot];
    return true;
}

circuit_check_result_t circuit_logic_evaluate(const board_snapshot_t *snapshot,
                                               const level_rule_t *rule,
                                               uint8_t input_values,
                                               uint8_t *output_values)
{
    if (snapshot == NULL || rule == NULL || output_values == NULL) {
        return result(CIRCUIT_CHECK_UNKNOWN_LEVEL, "这个任务还没有检查规则");
    }
    if (snapshot->invalid_link_count > 0U || snapshot->link_overflow) {
        return result(CIRCUIT_CHECK_INVALID_LINK, "有接线需要重新检查");
    }

    uint8_t input_slots[LEVEL_RULE_MAX_INPUTS] = {0};
    uint8_t output_slots[LEVEL_RULE_MAX_OUTPUTS] = {0};
    uint8_t input_count = 0;
    uint8_t output_count = 0;
    for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
        if (!snapshot->slots[slot].present) continue;
        if (!snapshot->slots[slot].id_valid) {
            return result(CIRCUIT_CHECK_UNKNOWN_BLOCK, "有一个积木身份还不认识");
        }
        if (snapshot->slots[slot].gate == SSD1315_GATE_INPUT) {
            if (input_count < LEVEL_RULE_MAX_INPUTS) input_slots[input_count] = slot;
            ++input_count;
        } else if (snapshot->slots[slot].gate == SSD1315_GATE_OUTPUT) {
            if (output_count < LEVEL_RULE_MAX_OUTPUTS) output_slots[output_count] = slot;
            ++output_count;
        }
    }
    if (input_count != rule->input_count) {
        return result(CIRCUIT_CHECK_WRONG_INPUT_COUNT, "输入积木的数量还不对");
    }
    if (output_count != rule->output_count) {
        return result(CIRCUIT_CHECK_WRONG_OUTPUT_COUNT, "输出积木的数量还不对");
    }

    circuit_signal_state_t signal;
    if (!circuit_logic_propagate(snapshot, input_slots, input_count, input_values, &signal)) {
        return result(CIRCUIT_CHECK_UNKNOWN_BLOCK, "有一个积木身份还不认识");
    }

    uint8_t outputs = 0;
    for (uint8_t output = 0; output < output_count; ++output) {
        bool value = false;
        bool connected = false;
        if (signal.known[output_slots[output]]) {
            value = signal.values[output_slots[output]];
            connected = true;
        }
        if (!connected) {
            return result(CIRCUIT_CHECK_INCOMPLETE, "电路还没有完整接通");
        }
        if (value) outputs |= 1U << output;
    }
    *output_values = outputs;
    return result(CIRCUIT_CHECK_OK, "可以开始检查啦");
}

circuit_check_result_t circuit_logic_precheck(const board_snapshot_t *snapshot,
                                               const level_rule_t *rule)
{
    uint8_t outputs;
    return circuit_logic_evaluate(snapshot, rule, 0, &outputs);
}
