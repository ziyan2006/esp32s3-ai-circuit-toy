#include "game_logic_self_test.h"

#include <string.h>

#include "circuit_layout.h"
#include "circuit_role_labels.h"
#include "circuit_debug.h"
#include "circuit_logic.h"
#include "esp_check.h"
#include "esp_log.h"
#include "level_rules.h"

static const char *TAG = "game_self_test";

static void add_slot(board_snapshot_t *snapshot, uint8_t slot, ssd1315_gate_t gate)
{
    snapshot->slots[slot] = (board_slot_identity_t) {
        .present = true,
        .id_valid = true,
        .raw_id = ssd1315_gate_to_eeprom_id(gate),
        .gate = gate,
    };
    const uint8_t base = slot * BOARD_SNAPSHOT_PORTS_PER_SLOT;
    for (uint8_t local = 0; local < BOARD_SNAPSHOT_PORTS_PER_SLOT; ++local) {
        snapshot->port_roles[base + local] = board_snapshot_gate_port_role(gate, slot, local);
    }
}

static void add_link(board_snapshot_t *snapshot, uint8_t first, uint8_t second)
{
    const uint8_t index = snapshot->link_count++;
    snapshot->links[index] = (board_link_t) {
        .first_port = first,
        .second_port = second,
        .color_index = index,
        .valid = true,
        .error = BOARD_LINK_OK,
    };
}

static uint8_t find_role_port(const board_snapshot_t *snapshot,
                              uint8_t slot,
                              board_port_role_t role,
                              uint8_t role_index)
{
    const uint8_t base = slot * BOARD_SNAPSHOT_PORTS_PER_SLOT;
    for (uint8_t local = 0; local < BOARD_SNAPSHOT_PORTS_PER_SLOT; ++local) {
        if (snapshot->port_roles[base + local] == role) {
            if (role_index-- == 0U) return base + local;
        }
    }
    return UINT8_MAX;
}

static esp_err_t test_rule_catalog(void)
{
    static const uint16_t ids[] = {
        101, 102, 103, 201, 202, 203, 301, 302, 401,
        402, 403, 501, 502, 503, 601, 602,
    };
    for (size_t index = 0; index < sizeof(ids) / sizeof(ids[0]); ++index) {
        const level_rule_t *rule = level_rule_get(ids[index]);
        ESP_RETURN_ON_FALSE(rule != NULL, ESP_FAIL, TAG, "missing rule %u", ids[index]);
        ESP_RETURN_ON_FALSE(rule->case_count == (1U << rule->input_count), ESP_FAIL, TAG,
                            "wrong case count %u", ids[index]);
    }

    const level_rule_t *half_adder = level_rule_get(403);
    ESP_RETURN_ON_FALSE(half_adder->expected_outputs[3] == 2U, ESP_FAIL, TAG,
                        "half adder 1+1 result");
    const level_rule_t *carry = level_rule_get(502);
    ESP_RETURN_ON_FALSE(carry != NULL && carry->rule_version == 1U, ESP_FAIL, TAG,
                        "carry rule version");
    ESP_RETURN_ON_FALSE(carry->expected_outputs[3] == 1U &&
                            carry->expected_outputs[4] == 0U,
                        ESP_FAIL, TAG, "carry truth table ordering");
    static const uint8_t full_adder_expected[8] = {0, 1, 1, 2, 1, 2, 2, 3};
    ESP_RETURN_ON_FALSE(level_rule_get(504) == NULL, ESP_FAIL, TAG,
                        "removed level 504 rule still exists");
    ESP_RETURN_ON_FALSE(memcmp(level_rule_get(503)->expected_outputs,
                               full_adder_expected, sizeof(full_adder_expected)) == 0,
                        ESP_FAIL, TAG, "full adder truth table");
    ESP_RETURN_ON_FALSE(level_rule_get(503)->expected_outputs[7] == 3U, ESP_FAIL, TAG,
                        "full adder all inputs high");
    static const uint8_t mux_expected[8] = {0, 1, 0, 1, 0, 0, 1, 1};
    ESP_RETURN_ON_FALSE(memcmp(level_rule_get(601)->expected_outputs,
                               mux_expected, sizeof(mux_expected)) == 0,
                        ESP_FAIL, TAG, "mux truth table");
    static const uint8_t decoder_expected[4] = {1, 2, 4, 8};
    ESP_RETURN_ON_FALSE(memcmp(level_rule_get(602)->expected_outputs,
                               decoder_expected, sizeof(decoder_expected)) == 0,
                        ESP_FAIL, TAG, "decoder truth table");
    return ESP_OK;
}

static esp_err_t test_role_labels(void)
{
    board_snapshot_t snapshot = {0};

    add_slot(&snapshot, 0, SSD1315_GATE_OUTPUT);
    add_slot(&snapshot, 6, SSD1315_GATE_INPUT);
    add_slot(&snapshot, 7, SSD1315_GATE_INPUT);
    circuit_role_labels_assign(401U, snapshot.slots);
    ESP_RETURN_ON_FALSE(strcmp(snapshot.slots[6].role_label, "A") == 0 &&
                            strcmp(snapshot.slots[7].role_label, "B") == 0 &&
                            strcmp(snapshot.slots[0].role_label, "个") == 0,
                        ESP_FAIL, TAG, "401 roles");

    circuit_role_labels_assign(402U, snapshot.slots);
    ESP_RETURN_ON_FALSE(strcmp(snapshot.slots[6].role_label, "A") == 0 &&
                            strcmp(snapshot.slots[7].role_label, "B") == 0 &&
                            strcmp(snapshot.slots[0].role_label, "进") == 0,
                        ESP_FAIL, TAG, "402 roles");

    memset(&snapshot, 0, sizeof(snapshot));
    add_slot(&snapshot, 0, SSD1315_GATE_OUTPUT);
    add_slot(&snapshot, 6, SSD1315_GATE_INPUT);
    add_slot(&snapshot, 7, SSD1315_GATE_INPUT);
    add_slot(&snapshot, 8, SSD1315_GATE_INPUT);

    circuit_role_labels_assign(601U, snapshot.slots);

    ESP_RETURN_ON_FALSE(strcmp(snapshot.slots[6].role_label, "A") == 0, ESP_FAIL, TAG,
                        "601 first input role");
    ESP_RETURN_ON_FALSE(strcmp(snapshot.slots[7].role_label, "B") == 0, ESP_FAIL, TAG,
                        "601 second input role");
    ESP_RETURN_ON_FALSE(strcmp(snapshot.slots[8].role_label, "选") == 0, ESP_FAIL, TAG,
                        "601 select input role");
    ESP_RETURN_ON_FALSE(strcmp(snapshot.slots[0].role_label, "Y") == 0, ESP_FAIL, TAG,
                        "601 output role");

    memset(&snapshot, 0, sizeof(snapshot));
    add_slot(&snapshot, 0, SSD1315_GATE_OUTPUT);
    add_slot(&snapshot, 1, SSD1315_GATE_OUTPUT);
    add_slot(&snapshot, 6, SSD1315_GATE_INPUT);
    circuit_role_labels_assign(403U, snapshot.slots);
    ESP_RETURN_ON_FALSE(snapshot.slots[6].role_label[0] == '\0' &&
                            snapshot.slots[0].role_label[0] == '\0',
                        ESP_FAIL, TAG, "403 incomplete roles hidden");

    add_slot(&snapshot, 7, SSD1315_GATE_INPUT);
    circuit_role_labels_assign(403U, snapshot.slots);
    ESP_RETURN_ON_FALSE(strcmp(snapshot.slots[6].role_label, "A") == 0 &&
                            strcmp(snapshot.slots[7].role_label, "B") == 0 &&
                            strcmp(snapshot.slots[0].role_label, "个") == 0 &&
                            strcmp(snapshot.slots[1].role_label, "进") == 0,
                        ESP_FAIL, TAG, "403 roles");

    memset(&snapshot, 0, sizeof(snapshot));
    add_slot(&snapshot, 0, SSD1315_GATE_OUTPUT);
    add_slot(&snapshot, 6, SSD1315_GATE_INPUT);
    add_slot(&snapshot, 7, SSD1315_GATE_INPUT);
    add_slot(&snapshot, 8, SSD1315_GATE_INPUT);
    circuit_role_labels_assign(501U, snapshot.slots);
    ESP_RETURN_ON_FALSE(strcmp(snapshot.slots[6].role_label, "A") == 0 &&
                            strcmp(snapshot.slots[7].role_label, "B") == 0 &&
                            strcmp(snapshot.slots[8].role_label, "C") == 0 &&
                            strcmp(snapshot.slots[0].role_label, "个") == 0,
                        ESP_FAIL, TAG, "501 roles");

    circuit_role_labels_assign(502U, snapshot.slots);
    ESP_RETURN_ON_FALSE(strcmp(snapshot.slots[6].role_label, "A") == 0 &&
                            strcmp(snapshot.slots[7].role_label, "B") == 0 &&
                            strcmp(snapshot.slots[8].role_label, "C") == 0 &&
                            strcmp(snapshot.slots[0].role_label, "进") == 0,
                        ESP_FAIL, TAG, "502 roles");

    add_slot(&snapshot, 1, SSD1315_GATE_OUTPUT);
    circuit_role_labels_assign(503U, snapshot.slots);
    ESP_RETURN_ON_FALSE(strcmp(snapshot.slots[6].role_label, "A") == 0 &&
                            strcmp(snapshot.slots[7].role_label, "B") == 0 &&
                            strcmp(snapshot.slots[8].role_label, "C") == 0 &&
                            strcmp(snapshot.slots[0].role_label, "个") == 0 &&
                            strcmp(snapshot.slots[1].role_label, "进") == 0,
                        ESP_FAIL, TAG, "503 roles");

    memset(&snapshot, 0, sizeof(snapshot));
    add_slot(&snapshot, 0, SSD1315_GATE_OUTPUT);
    add_slot(&snapshot, 1, SSD1315_GATE_OUTPUT);
    add_slot(&snapshot, 2, SSD1315_GATE_OUTPUT);
    add_slot(&snapshot, 3, SSD1315_GATE_OUTPUT);
    add_slot(&snapshot, 6, SSD1315_GATE_INPUT);
    add_slot(&snapshot, 7, SSD1315_GATE_INPUT);
    circuit_role_labels_assign(602U, snapshot.slots);
    ESP_RETURN_ON_FALSE(strcmp(snapshot.slots[6].role_label, "A") == 0 &&
                            strcmp(snapshot.slots[7].role_label, "B") == 0 &&
                            strcmp(snapshot.slots[0].role_label, "0") == 0 &&
                            strcmp(snapshot.slots[3].role_label, "3") == 0,
                        ESP_FAIL, TAG, "602 roles");
    return ESP_OK;
}

static esp_err_t test_direct_and_gates(void)
{
    board_snapshot_t snapshot = {.play_active = true};
    uint8_t output = 0;
    const level_rule_t *direct = level_rule_get(101);
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.play_active = true;
    add_slot(&snapshot, 0, SSD1315_GATE_INPUT);
    add_slot(&snapshot, 1, SSD1315_GATE_OUTPUT);
    add_link(&snapshot, 0, 4U);
    ESP_RETURN_ON_FALSE(
        circuit_logic_evaluate(&snapshot, direct, 0, &output).code == CIRCUIT_CHECK_OK && output == 0,
        ESP_FAIL, TAG, "direct low side mapping");
    ESP_RETURN_ON_FALSE(
        circuit_logic_evaluate(&snapshot, direct, 1, &output).code == CIRCUIT_CHECK_OK && output == 1,
        ESP_FAIL, TAG, "direct high side mapping");

    memset(&snapshot, 0, sizeof(snapshot));
    add_slot(&snapshot, 0, SSD1315_GATE_INPUT);
    add_slot(&snapshot, 1, SSD1315_GATE_INPUT);
    add_slot(&snapshot, 2, SSD1315_GATE_NAND);
    add_slot(&snapshot, 3, SSD1315_GATE_OUTPUT);
    add_link(&snapshot, 0, 11);
    add_link(&snapshot, 6, 9);
    add_link(&snapshot, 8, 12);
    const level_rule_t *nand = level_rule_get(102);
    for (uint8_t input = 0; input < nand->case_count; ++input) {
        ESP_RETURN_ON_FALSE(circuit_logic_evaluate(&snapshot, nand, input, &output).code == CIRCUIT_CHECK_OK &&
                            output == nand->expected_outputs[input], ESP_FAIL, TAG,
                            "nand row %u", input);
    }

    memset(&snapshot, 0, sizeof(snapshot));
    add_slot(&snapshot, 0, SSD1315_GATE_INPUT);
    add_slot(&snapshot, 1, SSD1315_GATE_NOT);
    add_slot(&snapshot, 2, SSD1315_GATE_OUTPUT);
    add_link(&snapshot, 0, 4);
    add_link(&snapshot, 6, 10);
    const level_rule_t *not_rule = level_rule_get(103);
    for (uint8_t input = 0; input < not_rule->case_count; ++input) {
        ESP_RETURN_ON_FALSE(circuit_logic_evaluate(&snapshot, not_rule, input, &output).code == CIRCUIT_CHECK_OK &&
                            output == not_rule->expected_outputs[input], ESP_FAIL, TAG,
                            "not row %u", input);
    }
    snapshot.invalid_link_count = 1;
    ESP_RETURN_ON_FALSE(circuit_logic_precheck(&snapshot, not_rule).code == CIRCUIT_CHECK_INVALID_LINK,
                        ESP_FAIL, TAG, "invalid link precheck");
    return ESP_OK;
}

static esp_err_t test_input_four_port_fanout(void)
{
    board_slot_identity_t slots[BOARD_SNAPSHOT_SLOT_COUNT] = {0};
    slots[0] = (board_slot_identity_t) {
        .present = true,
        .id_valid = true,
        .raw_id = ssd1315_gate_to_eeprom_id(SSD1315_GATE_INPUT),
        .gate = SSD1315_GATE_INPUT,
    };
    slots[1] = (board_slot_identity_t) {
        .present = true,
        .id_valid = true,
        .raw_id = ssd1315_gate_to_eeprom_id(SSD1315_GATE_NAND),
        .gate = SSD1315_GATE_NAND,
    };

    baseboard_ir_matrix_t matrix = {0};
    /* Input slot 0 fans out through two physical ports to NAND's two inputs. */
    matrix.logical_rx[0] = 1ULL << 5U;
    matrix.logical_rx[1] = 1ULL << 7U;
    matrix.completed_scans = 1U;
    board_snapshot_publish_slots(slots, 1U);
    board_snapshot_publish_ir(&matrix);

    board_snapshot_t snapshot;
    ESP_RETURN_ON_FALSE(board_snapshot_get(&snapshot) && snapshot.link_count == 2U &&
                            snapshot.invalid_link_count == 0U &&
                            snapshot.port_roles[0] == BOARD_PORT_OUTPUT &&
                            snapshot.port_roles[1] == BOARD_PORT_OUTPUT &&
                            snapshot.port_roles[2] == BOARD_PORT_OUTPUT &&
                            snapshot.port_roles[3] == BOARD_PORT_OUTPUT &&
                            snapshot.links[0].valid && snapshot.links[1].valid,
                        ESP_FAIL, TAG, "input four-port fanout links");

    const uint8_t input_slots[] = {0U};
    circuit_signal_state_t signals;
    ESP_RETURN_ON_FALSE(circuit_logic_propagate(&snapshot, input_slots, 1U, 0U, &signals) &&
                            signals.known[1] && signals.values[1],
                        ESP_FAIL, TAG, "input fanout NAND low");
    ESP_RETURN_ON_FALSE(circuit_logic_propagate(&snapshot, input_slots, 1U, 1U, &signals) &&
                            signals.known[1] && !signals.values[1],
                        ESP_FAIL, TAG, "input fanout NAND high");
    board_snapshot_reset(false);
    return ESP_OK;
}

static esp_err_t test_debug_signal_and_fixed_inputs(void)
{
    board_snapshot_t snapshot = {.play_active = true};
    add_slot(&snapshot, 0, SSD1315_GATE_INPUT);
    add_slot(&snapshot, 1, SSD1315_GATE_INPUT);
    add_slot(&snapshot, 2, SSD1315_GATE_OUTPUT);
    const uint8_t source = find_role_port(&snapshot, 1, BOARD_PORT_OUTPUT, 0);
    const uint8_t sink = find_role_port(&snapshot, 2, BOARD_PORT_INPUT, 0);
    ESP_RETURN_ON_FALSE(source != UINT8_MAX && sink != UINT8_MAX,
                        ESP_FAIL, TAG, "debug role ports");
    add_link(&snapshot, source, sink);

    const uint8_t explicit_order[] = {1, 0};
    circuit_signal_state_t signals;
    ESP_RETURN_ON_FALSE(circuit_logic_propagate(&snapshot, explicit_order, 2, 1U, &signals) &&
                            signals.known[1] && signals.values[1] &&
                            signals.known[0] && !signals.values[0] &&
                            signals.known[2] && signals.values[2],
                        ESP_FAIL, TAG, "debug explicit input order");
    bool link_value = false;
    ESP_RETURN_ON_FALSE(circuit_logic_get_link_signal(&snapshot, &signals,
                                                       &snapshot.links[0], &link_value) &&
                            link_value,
                        ESP_FAIL, TAG, "debug link signal");

    circuit_debug_reset();
    snapshot.generation = 1U;
    snapshot.completed_ir_scans = 1U;
    circuit_debug_sync_snapshot(&snapshot);
    board_snapshot_t stable;
    ESP_RETURN_ON_FALSE(circuit_debug_get_render_snapshot(&stable) && stable.link_count == 1U,
                        ESP_FAIL, TAG, "debug stable link initial");
    snapshot.generation = 2U;
    snapshot.completed_ir_scans = 2U;
    snapshot.link_count = 0U;
    circuit_debug_sync_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(circuit_debug_get_render_snapshot(&stable) && stable.link_count == 1U,
                        ESP_FAIL, TAG, "debug stable link first miss");
    snapshot.generation = 3U;
    circuit_debug_sync_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(circuit_debug_get_render_snapshot(&stable) && stable.link_count == 1U,
                        ESP_FAIL, TAG, "debug stable link polling does not count");
    snapshot.generation = 4U;
    snapshot.completed_ir_scans = 3U;
    circuit_debug_sync_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(circuit_debug_get_render_snapshot(&stable) && stable.link_count == 0U,
                        ESP_FAIL, TAG, "debug stable link second miss");

    circuit_debug_reset();
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.play_active = true;
    add_slot(&snapshot, 6, SSD1315_GATE_INPUT);  /* physical slot 7 / SW4 */
    add_slot(&snapshot, 7, SSD1315_GATE_AND);    /* target slot, but not an input */
    add_slot(&snapshot, 8, SSD1315_GATE_INPUT);  /* physical slot 9 / SW2 */
    add_slot(&snapshot, 2, SSD1315_GATE_INPUT);  /* misplaced input */
    circuit_debug_sync_snapshot(&snapshot);
    circuit_debug_update_switches(0x02U);        /* SW2 only */
    circuit_debug_set_enabled(true);

    circuit_debug_state_t debug;
    ESP_RETURN_ON_FALSE(circuit_debug_get_state(&debug) && debug.enabled &&
                            debug.switch_mask == 0x02U && debug.input_count == 2U &&
                            debug.input_slots[0] == 6U && debug.input_slots[1] == 8U &&
                            debug.misplaced_input_mask == (uint16_t)(1U << 2) &&
                            circuit_debug_compact_input_values(&debug) == 0x02U,
                        ESP_FAIL, TAG, "debug fixed slot binding");

    memset(&snapshot.slots[2], 0, sizeof(snapshot.slots[2]));
    add_slot(&snapshot, 9, SSD1315_GATE_INPUT);  /* physical slot 10 / SW1 */
    snapshot.generation = 2U;
    circuit_debug_sync_snapshot(&snapshot);
    circuit_debug_update_switches(0x01U);        /* SW1 only */
    ESP_RETURN_ON_FALSE(circuit_debug_get_state(&debug) && debug.input_count == 3U &&
                            debug.input_slots[0] == 6U && debug.input_slots[1] == 8U &&
                            debug.input_slots[2] == 9U && debug.misplaced_input_mask == 0U &&
                            circuit_debug_compact_input_values(&debug) == 0x04U,
                        ESP_FAIL, TAG, "debug fixed slot gap mapping");

    memset(&snapshot.slots[6], 0, sizeof(snapshot.slots[6]));
    snapshot.generation = 3U;
    circuit_debug_sync_snapshot(&snapshot);
    ESP_RETURN_ON_FALSE(circuit_debug_get_state(&debug) && debug.input_count == 2U &&
                            debug.input_slots[0] == 8U && debug.input_slots[1] == 9U &&
                            circuit_debug_compact_input_values(&debug) == 0x02U,
                        ESP_FAIL, TAG, "debug fixed slot removal");
    circuit_debug_reset();
    return ESP_OK;
}

static esp_err_t test_layout_bounds(void)
{
    board_snapshot_t snapshot = {0};
    for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
        add_slot(&snapshot, slot, SSD1315_GATE_AND);
    }
    circuit_layout_t layout;
    circuit_layout_compute(&snapshot, 660, 492, &layout);
    ESP_RETURN_ON_FALSE(layout.node_count == BOARD_SNAPSHOT_SLOT_COUNT, ESP_FAIL, TAG,
                        "layout node count");
    for (uint8_t index = 0; index < layout.node_count; ++index) {
        const circuit_layout_node_t *node = &layout.nodes[index];
        ESP_RETURN_ON_FALSE(node->x >= 0 && node->y >= 0 &&
                            node->x + node->width <= 660 && node->y + node->height <= 492,
                            ESP_FAIL, TAG, "layout bounds slot %u", node->slot);
    }
    return ESP_OK;
}

static esp_err_t test_layout_routes_avoid_nodes(void)
{
    board_snapshot_t snapshot = {0};
    for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
        const ssd1315_gate_t gate = slot < 3U ? SSD1315_GATE_INPUT :
                                      slot < 6U ? SSD1315_GATE_OUTPUT :
                                      (slot & 1U) != 0U ? SSD1315_GATE_AND : SSD1315_GATE_OR;
        add_slot(&snapshot, slot, gate);
    }

    circuit_layout_t layout;
    circuit_layout_compute(&snapshot, 660, 492, &layout);
    ESP_RETURN_ON_FALSE(layout.node_count == BOARD_SNAPSHOT_SLOT_COUNT, ESP_FAIL, TAG,
                        "route test node count");
    for (uint8_t first = 0; first < layout.node_count; ++first) {
        for (uint8_t second = first; second < layout.node_count; ++second) {
            for (uint8_t first_local = 0; first_local < BOARD_SNAPSHOT_PORTS_PER_SLOT;
                 ++first_local) {
                for (uint8_t second_local = 0; second_local < BOARD_SNAPSHOT_PORTS_PER_SLOT;
                     ++second_local) {
                    if (first == second && first_local == second_local) continue;
                    circuit_layout_route_t route;
                    const uint8_t lane = (uint8_t)(first * 7U + second * 3U +
                                                   first_local * 2U + second_local);
                    ESP_RETURN_ON_FALSE(
                        circuit_layout_route(&layout, 660, 492, first, first_local,
                                             second, second_local, lane, &route) &&
                        circuit_layout_route_avoids_nodes(&layout, &route),
                        ESP_FAIL, TAG, "route nodes %u/%u ports %u/%u",
                        first, second, first_local, second_local);
                }
            }
        }
    }
    return ESP_OK;
}

static esp_err_t test_ignored_port_policy(void)
{
    board_slot_identity_t slots[BOARD_SNAPSHOT_SLOT_COUNT] = {0};
    slots[0] = (board_slot_identity_t) {
        .present = true,
        .id_valid = true,
        .gate = SSD1315_GATE_INPUT,
    };
    slots[1] = (board_slot_identity_t) {
        .present = true,
        .id_valid = true,
        .gate = SSD1315_GATE_OUTPUT,
    };

    baseboard_ir_matrix_t matrix = {0};
    matrix.logical_rx[3] = 1ULL << 7U;
    matrix.completed_scans = 1;
    board_snapshot_publish_slots(slots, 1);
    board_snapshot_publish_ir(&matrix);

    board_snapshot_t snapshot;
    const bool read_ok = board_snapshot_get(&snapshot);
    ESP_RETURN_ON_FALSE(read_ok && snapshot.link_count == 1U &&
                            snapshot.ignored_link_count == 1U &&
                            snapshot.invalid_link_count == 0U &&
                            board_link_is_ignored(&snapshot.links[0]) &&
                            snapshot.links[0].color_index != BOARD_SNAPSHOT_INVALID_COLOR,
                        ESP_FAIL, TAG, "ignored port policy");
    board_snapshot_reset(false);
    return ESP_OK;
}

esp_err_t game_logic_self_test_run(void)
{
    ESP_RETURN_ON_ERROR(board_mapping_validate(), TAG, "board mapping");
    ESP_RETURN_ON_ERROR(test_rule_catalog(), TAG, "rule catalog");
    ESP_RETURN_ON_ERROR(test_role_labels(), TAG, "role labels");
    ESP_RETURN_ON_ERROR(test_direct_and_gates(), TAG, "circuit evaluator");
    ESP_RETURN_ON_ERROR(test_input_four_port_fanout(), TAG, "input fanout");
    ESP_RETURN_ON_ERROR(test_debug_signal_and_fixed_inputs(), TAG, "circuit debug");
    ESP_RETURN_ON_ERROR(test_layout_bounds(), TAG, "circuit layout");
    ESP_RETURN_ON_ERROR(test_layout_routes_avoid_nodes(), TAG, "circuit routes");
    ESP_RETURN_ON_ERROR(test_ignored_port_policy(), TAG, "ignored port policy");
    ESP_LOGI(TAG, "17 level rules, circuit/debug evaluator, 16-slot layout and obstacle routing passed");
    return ESP_OK;
}
