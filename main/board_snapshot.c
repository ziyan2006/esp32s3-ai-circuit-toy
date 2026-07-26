#include "board_snapshot.h"

#include <string.h>

#include "freertos/FreeRTOS.h"

static const board_connection_color_t s_palette[BOARD_SNAPSHOT_PALETTE_SIZE] = {
    {94, 220, 244}, {240, 166, 202}, {244, 208, 63}, {117, 230, 176},
    {255, 120, 149}, {120, 170, 255}, {189, 128, 255}, {255, 165, 84},
    {86, 235, 207}, {255, 105, 180}, {164, 224, 79}, {101, 190, 255},
    {255, 214, 112}, {151, 132, 255}, {78, 205, 140}, {255, 135, 96},
    {92, 238, 255}, {230, 120, 230}, {178, 238, 102}, {255, 184, 214},
    {112, 156, 255}, {255, 225, 72}, {92, 224, 186}, {235, 142, 92},
    {255, 155, 72}, {72, 210, 255}, {215, 255, 105}, {255, 110, 210},
    {110, 255, 245}, {205, 150, 255}, {255, 195, 110}, {130, 220, 100},
};
static const board_connection_color_t s_invalid_color = {255, 120, 149};

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static board_snapshot_t s_snapshot;
static board_slot_identity_t s_source_slots[BOARD_SNAPSHOT_SLOT_COUNT];
static uint64_t s_source_logical_rx[BOARD_SNAPSHOT_PORT_COUNT];
static uint32_t s_source_ir_scans;
static uint32_t s_source_i2c_scans;

board_port_role_t board_snapshot_gate_port_role(ssd1315_gate_t gate,
                                                uint8_t slot,
                                                uint8_t local_port)
{
    if (slot >= BOARD_SNAPSHOT_SLOT_COUNT || local_port >= BOARD_SNAPSHOT_PORTS_PER_SLOT) {
        return BOARD_PORT_UNUSED;
    }
    const board_port_side_t side = board_mapping_port_side(slot, local_port);
    if (gate == SSD1315_GATE_INPUT) {
        /* All four physical sides are equivalent output taps for an input
         * block. This permits fan-out without requiring a preferred side. */
        (void)side;
        return BOARD_PORT_OUTPUT;
    }
    if (gate == SSD1315_GATE_OUTPUT) {
        return side == BOARD_PORT_SIDE_LEFT ? BOARD_PORT_INPUT : BOARD_PORT_UNUSED;
    }
    if (gate == SSD1315_GATE_NOT) {
        if (side == BOARD_PORT_SIDE_LEFT) return BOARD_PORT_INPUT;
        if (side == BOARD_PORT_SIDE_RIGHT) return BOARD_PORT_OUTPUT;
        return BOARD_PORT_UNUSED;
    }
    if (gate >= SSD1315_GATE_AND && gate <= SSD1315_GATE_XNOR) {
        if (side == BOARD_PORT_SIDE_UP || side == BOARD_PORT_SIDE_DOWN) {
            return BOARD_PORT_INPUT;
        }
        if (side == BOARD_PORT_SIDE_RIGHT) return BOARD_PORT_OUTPUT;
    }
    return BOARD_PORT_UNUSED;
}

static int previous_color_for_pair(uint8_t first, uint8_t second)
{
    for (uint8_t index = 0; index < s_snapshot.link_count; ++index) {
        const board_link_t *link = &s_snapshot.links[index];
        if ((link->valid || link->error == BOARD_LINK_UNUSED_PORT) &&
            link->first_port == first && link->second_port == second) {
            return link->color_index;
        }
    }
    return -1;
}

static bool topology_equal(const board_snapshot_t *first, const board_snapshot_t *second)
{
    if (memcmp(first->slots, second->slots, sizeof(first->slots)) != 0 ||
        memcmp(first->port_roles, second->port_roles, sizeof(first->port_roles)) != 0 ||
        first->link_count != second->link_count ||
        first->ignored_link_count != second->ignored_link_count ||
        first->invalid_link_count != second->invalid_link_count ||
        first->link_overflow != second->link_overflow) {
        return false;
    }
    return memcmp(first->links, second->links,
                  (size_t)first->link_count * sizeof(first->links[0])) == 0;
}

static void rebuild_locked(void)
{
    board_snapshot_t next = {
        .play_active = s_snapshot.play_active,
        .generation = s_snapshot.generation + 1U,
        .topology_revision = s_snapshot.topology_revision,
        .completed_ir_scans = s_source_ir_scans,
        .completed_i2c_scans = s_source_i2c_scans,
    };
    memcpy(next.slots, s_source_slots, sizeof(next.slots));

    for (uint8_t port = 0; port < BOARD_SNAPSHOT_PORT_COUNT; ++port) {
        const uint8_t slot = board_mapping_slot_for_port(port);
        const uint8_t local_port = board_mapping_local_port(port);
        if (next.slots[slot].present && next.slots[slot].id_valid) {
            next.port_roles[port] = board_snapshot_gate_port_role(next.slots[slot].gate,
                                                                  slot, local_port);
        }
    }

    uint8_t degree[BOARD_SNAPSHOT_PORT_COUNT] = {0};
    for (uint8_t first = 0; first < BOARD_SNAPSHOT_PORT_COUNT; ++first) {
        for (uint8_t second = first + 1U; second < BOARD_SNAPSHOT_PORT_COUNT; ++second) {
            const bool connected = ((s_source_logical_rx[first] & (1ULL << second)) != 0ULL) ||
                                   ((s_source_logical_rx[second] & (1ULL << first)) != 0ULL);
            if (connected) {
                if (degree[first] != UINT8_MAX) ++degree[first];
                if (degree[second] != UINT8_MAX) ++degree[second];
            }
        }
    }

    bool palette_used[BOARD_SNAPSHOT_PALETTE_SIZE] = {0};
    for (uint8_t first = 0; first < BOARD_SNAPSHOT_PORT_COUNT; ++first) {
        for (uint8_t second = first + 1U; second < BOARD_SNAPSHOT_PORT_COUNT; ++second) {
            const bool connected = ((s_source_logical_rx[first] & (1ULL << second)) != 0ULL) ||
                                   ((s_source_logical_rx[second] & (1ULL << first)) != 0ULL);
            if (!connected) continue;
            if (next.link_count >= BOARD_SNAPSHOT_MAX_LINKS) {
                next.link_overflow = true;
                continue;
            }

            board_link_t *link = &next.links[next.link_count++];
            link->first_port = first;
            link->second_port = second;
            link->color_index = BOARD_SNAPSHOT_INVALID_COLOR;
            const uint8_t first_slot = board_mapping_slot_for_port(first);
            const uint8_t second_slot = board_mapping_slot_for_port(second);
            if (degree[first] > 1U || degree[second] > 1U) {
                link->error = BOARD_LINK_MULTIPLE_CONNECTIONS;
            } else if (!next.slots[first_slot].present || !next.slots[first_slot].id_valid ||
                       !next.slots[second_slot].present || !next.slots[second_slot].id_valid) {
                link->error = BOARD_LINK_UNKNOWN_ENDPOINT;
            } else if (next.port_roles[first] == BOARD_PORT_UNUSED ||
                       next.port_roles[second] == BOARD_PORT_UNUSED) {
                link->error = BOARD_LINK_UNUSED_PORT;
            } else if (!((next.port_roles[first] == BOARD_PORT_OUTPUT &&
                          next.port_roles[second] == BOARD_PORT_INPUT) ||
                         (next.port_roles[second] == BOARD_PORT_OUTPUT &&
                          next.port_roles[first] == BOARD_PORT_INPUT))) {
                link->error = BOARD_LINK_DIRECTION_ERROR;
            } else {
                link->valid = true;
                link->error = BOARD_LINK_OK;
            }
            if (link->error == BOARD_LINK_UNUSED_PORT) {
                ++next.ignored_link_count;
            } else if (!link->valid) {
                ++next.invalid_link_count;
            }

            const int previous = previous_color_for_pair(first, second);
            if ((link->valid || link->error == BOARD_LINK_UNUSED_PORT) &&
                previous >= 0 && previous < BOARD_SNAPSHOT_PALETTE_SIZE &&
                !palette_used[previous]) {
                link->color_index = (uint8_t)previous;
                palette_used[previous] = true;
            }
        }
    }

    for (uint8_t index = 0; index < next.link_count; ++index) {
        board_link_t *link = &next.links[index];
        if ((!link->valid && link->error != BOARD_LINK_UNUSED_PORT) ||
            link->color_index != BOARD_SNAPSHOT_INVALID_COLOR) continue;
        for (uint8_t color = 0; color < BOARD_SNAPSHOT_PALETTE_SIZE; ++color) {
            if (!palette_used[color]) {
                link->color_index = color;
                palette_used[color] = true;
                break;
            }
        }
        if (link->color_index == BOARD_SNAPSHOT_INVALID_COLOR) link->color_index = 0;
    }

    if (!topology_equal(&s_snapshot, &next)) ++next.topology_revision;
    s_snapshot = next;
}

void board_snapshot_init(void)
{
    board_snapshot_reset(false);
}

void board_snapshot_reset(bool play_active)
{
    portENTER_CRITICAL(&s_lock);
    const uint32_t generation = s_snapshot.generation + 1U;
    const uint32_t topology_revision = s_snapshot.topology_revision + 1U;
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    memset(s_source_slots, 0, sizeof(s_source_slots));
    memset(s_source_logical_rx, 0, sizeof(s_source_logical_rx));
    s_source_ir_scans = 0;
    s_source_i2c_scans = 0;
    s_snapshot.play_active = play_active;
    s_snapshot.generation = generation;
    s_snapshot.topology_revision = topology_revision;
    portEXIT_CRITICAL(&s_lock);
}

void board_snapshot_publish_ir(const baseboard_ir_matrix_t *matrix)
{
    if (matrix == NULL) return;
    portENTER_CRITICAL(&s_lock);
    memcpy(s_source_logical_rx, matrix->logical_rx, sizeof(s_source_logical_rx));
    s_source_ir_scans = matrix->completed_scans;
    rebuild_locked();
    portEXIT_CRITICAL(&s_lock);
}

void board_snapshot_publish_slots(const board_slot_identity_t *slots, uint32_t completed_scans)
{
    if (slots == NULL) return;
    portENTER_CRITICAL(&s_lock);
    memcpy(s_source_slots, slots, sizeof(s_source_slots));
    s_source_i2c_scans = completed_scans;
    rebuild_locked();
    portEXIT_CRITICAL(&s_lock);
}

bool board_snapshot_get(board_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    portENTER_CRITICAL(&s_lock);
    *snapshot = s_snapshot;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

board_connection_color_t board_snapshot_get_color(uint8_t color_index)
{
    return color_index < BOARD_SNAPSHOT_PALETTE_SIZE ? s_palette[color_index] : s_invalid_color;
}
