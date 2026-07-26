#include "circuit_layout.h"

#include <stdbool.h>
#include <string.h>

#include "board_mapping.h"

#define LAYOUT_COLUMN_COUNT 4U
#define LAYOUT_NODE_WIDTH   126
#define LAYOUT_TOP          42
#define LAYOUT_BOTTOM       12
#define LAYOUT_SIDE         14
#define ROUTE_CLEARANCE     8
#define ROUTE_EDGE_MARGIN   6

static bool link_direction(const board_snapshot_t *snapshot,
                           const board_link_t *link,
                           uint8_t *source_slot,
                           uint8_t *target_slot)
{
    if (!link->valid) return false;
    uint8_t source_port;
    uint8_t target_port;
    if (snapshot->port_roles[link->first_port] == BOARD_PORT_OUTPUT) {
        source_port = link->first_port;
        target_port = link->second_port;
    } else {
        source_port = link->second_port;
        target_port = link->first_port;
    }
    *source_slot = board_mapping_slot_for_port(source_port);
    *target_slot = board_mapping_slot_for_port(target_port);
    return true;
}

void circuit_layout_compute(const board_snapshot_t *snapshot,
                            int16_t width,
                            int16_t height,
                            circuit_layout_t *layout)
{
    memset(layout, 0, sizeof(*layout));
    memset(layout->node_for_slot, -1, sizeof(layout->node_for_slot));
    if (snapshot == NULL) return;

    uint8_t depth[BOARD_SNAPSHOT_SLOT_COUNT] = {0};
    bool depth_known[BOARD_SNAPSHOT_SLOT_COUNT] = {0};
    for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
        if (snapshot->slots[slot].present && snapshot->slots[slot].gate == SSD1315_GATE_INPUT) {
            depth_known[slot] = true;
        }
    }
    for (uint8_t pass = 0; pass < BOARD_SNAPSHOT_SLOT_COUNT; ++pass) {
        bool progress = false;
        for (uint8_t index = 0; index < snapshot->link_count; ++index) {
            uint8_t source;
            uint8_t target;
            if (!link_direction(snapshot, &snapshot->links[index], &source, &target) ||
                !depth_known[source]) continue;
            const uint8_t next_depth = depth[source] < 10U ? depth[source] + 1U : 11U;
            if (!depth_known[target] || next_depth > depth[target]) {
                depth[target] = next_depth;
                depth_known[target] = true;
                progress = true;
            }
        }
        if (!progress) break;
    }

    uint8_t column_for_slot[BOARD_SNAPSHOT_SLOT_COUNT] = {0};
    uint8_t gate_slots[BOARD_SNAPSHOT_SLOT_COUNT];
    uint8_t gate_count = 0;
    uint8_t maximum_gate_depth = 0;
    for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
        if (!snapshot->slots[slot].present) continue;
        const ssd1315_gate_t gate = snapshot->slots[slot].gate;
        if (gate == SSD1315_GATE_INPUT) {
            column_for_slot[slot] = 0;
        } else if (gate == SSD1315_GATE_OUTPUT) {
            column_for_slot[slot] = 3;
        } else {
            gate_slots[gate_count++] = slot;
            if (depth_known[slot] && depth[slot] > maximum_gate_depth) maximum_gate_depth = depth[slot];
        }
    }
    uint8_t middle_counts[2] = {0};
    for (uint8_t index = 0; index < gate_count; ++index) {
        const uint8_t slot = gate_slots[index];
        uint8_t middle;
        if (depth_known[slot] && maximum_gate_depth > 1U) {
            middle = (uint8_t)((uint16_t)(depth[slot] - 1U) * 2U / maximum_gate_depth);
            if (middle > 1U) middle = 1U;
        } else {
            middle = middle_counts[0] <= middle_counts[1] ? 0U : 1U;
        }
        column_for_slot[slot] = middle + 1U;
        ++middle_counts[middle];
    }

    uint8_t column_slots[LAYOUT_COLUMN_COUNT][BOARD_SNAPSHOT_SLOT_COUNT] = {0};
    uint8_t column_counts[LAYOUT_COLUMN_COUNT] = {0};
    for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
        if (!snapshot->slots[slot].present) continue;
        const uint8_t column = column_for_slot[slot];
        column_slots[column][column_counts[column]++] = slot;
    }

    const int16_t column_gap = (int16_t)((width - 2 * LAYOUT_SIDE -
                                          LAYOUT_COLUMN_COUNT * LAYOUT_NODE_WIDTH) /
                                         (LAYOUT_COLUMN_COUNT - 1U));
    const int16_t available_height = height - LAYOUT_TOP - LAYOUT_BOTTOM;
    for (uint8_t column = 0; column < LAYOUT_COLUMN_COUNT; ++column) {
        const uint8_t count = column_counts[column];
        if (count == 0U) continue;
        const int16_t step = available_height / count;
        int16_t node_height = step - 8;
        if (node_height > 64) node_height = 64;
        if (node_height < 30) node_height = 30;
        for (uint8_t row = 0; row < count; ++row) {
            const uint8_t slot = column_slots[column][row];
            const uint8_t node_index = layout->node_count++;
            layout->node_for_slot[slot] = (int8_t)node_index;
            circuit_layout_node_t *node = &layout->nodes[node_index];
            node->slot = slot;
            node->width = LAYOUT_NODE_WIDTH;
            node->height = node_height;
            node->x = LAYOUT_SIDE + column * (LAYOUT_NODE_WIDTH + column_gap);
            node->y = LAYOUT_TOP + row * step + (step - node_height) / 2;
            for (uint8_t local = 0; local < BOARD_SNAPSHOT_PORTS_PER_SLOT; ++local) {
                const board_port_side_t side = board_mapping_port_side(slot, local);
                switch (side) {
                case BOARD_PORT_SIDE_UP:
                    node->port_x[local] = node->x;
                    node->port_y[local] = node->y + node_height / 3;
                    break;
                case BOARD_PORT_SIDE_DOWN:
                    node->port_x[local] = node->x;
                    node->port_y[local] = node->y + (node_height * 2) / 3;
                    break;
                case BOARD_PORT_SIDE_LEFT:
                    node->port_x[local] = node->x;
                    node->port_y[local] = node->y + node_height / 2;
                    break;
                case BOARD_PORT_SIDE_RIGHT:
                    node->port_x[local] = node->x + node->width;
                    node->port_y[local] = node->y + node_height / 2;
                    break;
                }
            }
        }
    }
}

static int32_t maximum(int32_t first, int32_t second)
{
    return first > second ? first : second;
}

static int32_t minimum(int32_t first, int32_t second)
{
    return first < second ? first : second;
}

static int32_t absolute(int32_t value)
{
    return value < 0 ? -value : value;
}

static bool route_add_point(circuit_layout_route_t *route, int32_t x, int32_t y)
{
    if (route->point_count > 0U &&
        route->x[route->point_count - 1U] == x &&
        route->y[route->point_count - 1U] == y) {
        return true;
    }
    if (route->point_count >= CIRCUIT_LAYOUT_ROUTE_MAX_POINTS) return false;
    route->x[route->point_count] = (int16_t)x;
    route->y[route->point_count] = (int16_t)y;
    ++route->point_count;
    return true;
}

static bool port_is_left(const circuit_layout_node_t *node, uint8_t local_port)
{
    return node->port_x[local_port] <= node->x + node->width / 2;
}

static bool corridor_lane(const circuit_layout_t *layout,
                          int16_t width,
                          const circuit_layout_node_t *node,
                          bool left,
                          uint8_t lane,
                          int32_t *result)
{
    static const int8_t lane_offsets[] = {0, -5, 5, -10, 10};
    int32_t low = ROUTE_EDGE_MARGIN;
    int32_t high = width - ROUTE_EDGE_MARGIN;
    const int32_t node_left = node->x;
    const int32_t node_right = node->x + node->width;

    if (left) {
        high = node_left - ROUTE_CLEARANCE;
        for (uint8_t index = 0; index < layout->node_count; ++index) {
            const circuit_layout_node_t *other = &layout->nodes[index];
            const int32_t other_right = other->x + other->width;
            if (other_right <= node_left) {
                low = maximum(low, other_right + ROUTE_CLEARANCE);
            }
        }
    } else {
        low = node_right + ROUTE_CLEARANCE;
        for (uint8_t index = 0; index < layout->node_count; ++index) {
            const circuit_layout_node_t *other = &layout->nodes[index];
            if (other->x >= node_right) {
                high = minimum(high, other->x - ROUTE_CLEARANCE);
            }
        }
    }
    if (low > high) return false;

    int32_t selected = (low + high) / 2 + lane_offsets[lane % 5U];
    if (selected < low) selected = low;
    if (selected > high) selected = high;
    *result = selected;
    return true;
}

bool circuit_layout_route(const circuit_layout_t *layout,
                          int16_t width,
                          int16_t height,
                          uint8_t first_node_index,
                          uint8_t first_local_port,
                          uint8_t second_node_index,
                          uint8_t second_local_port,
                          uint8_t lane,
                          circuit_layout_route_t *route)
{
    if (layout == NULL || route == NULL || width <= 0 || height <= 0 ||
        first_node_index >= layout->node_count || second_node_index >= layout->node_count ||
        first_local_port >= BOARD_SNAPSHOT_PORTS_PER_SLOT ||
        second_local_port >= BOARD_SNAPSHOT_PORTS_PER_SLOT) {
        return false;
    }

    memset(route, 0, sizeof(*route));
    const circuit_layout_node_t *first = &layout->nodes[first_node_index];
    const circuit_layout_node_t *second = &layout->nodes[second_node_index];
    const int32_t x1 = first->port_x[first_local_port];
    const int32_t y1 = first->port_y[first_local_port];
    const int32_t x2 = second->port_x[second_local_port];
    const int32_t y2 = second->port_y[second_local_port];
    int32_t first_spine;
    int32_t second_spine;
    if (!corridor_lane(layout, width, first, port_is_left(first, first_local_port),
                       lane, &first_spine) ||
        !corridor_lane(layout, width, second, port_is_left(second, second_local_port),
                       lane, &second_spine)) {
        return false;
    }

    if (!route_add_point(route, x1, y1) ||
        !route_add_point(route, first_spine, y1)) {
        return false;
    }
    if (first_spine == second_spine) {
        return route_add_point(route, second_spine, y2) &&
               route_add_point(route, x2, y2) &&
               circuit_layout_route_avoids_nodes(layout, route);
    }

    int32_t top = height;
    int32_t bottom = 0;
    for (uint8_t index = 0; index < layout->node_count; ++index) {
        const circuit_layout_node_t *node = &layout->nodes[index];
        top = minimum(top, node->y);
        bottom = maximum(bottom, node->y + node->height);
    }
    const int32_t route_offset = ROUTE_CLEARANCE + (lane % 4U) * 4;
    int32_t top_route = top - route_offset;
    int32_t bottom_route = bottom + route_offset;
    if (top_route < ROUTE_EDGE_MARGIN) top_route = ROUTE_EDGE_MARGIN;
    if (bottom_route > height - ROUTE_EDGE_MARGIN) bottom_route = height - ROUTE_EDGE_MARGIN;
    const int32_t top_cost = absolute(y1 - top_route) + absolute(y2 - top_route);
    const int32_t bottom_cost = absolute(y1 - bottom_route) + absolute(y2 - bottom_route);
    const int32_t route_y = top_cost < bottom_cost ||
                            (top_cost == bottom_cost && (lane & 1U) == 0U) ?
                            top_route : bottom_route;

    return route_add_point(route, first_spine, route_y) &&
           route_add_point(route, second_spine, route_y) &&
           route_add_point(route, second_spine, y2) &&
           route_add_point(route, x2, y2) &&
           circuit_layout_route_avoids_nodes(layout, route);
}

bool circuit_layout_route_avoids_nodes(const circuit_layout_t *layout,
                                       const circuit_layout_route_t *route)
{
    if (layout == NULL || route == NULL || route->point_count < 2U) return false;
    for (uint8_t segment = 0; segment + 1U < route->point_count; ++segment) {
        const int32_t x1 = route->x[segment];
        const int32_t y1 = route->y[segment];
        const int32_t x2 = route->x[segment + 1U];
        const int32_t y2 = route->y[segment + 1U];
        if (x1 != x2 && y1 != y2) return false;

        for (uint8_t index = 0; index < layout->node_count; ++index) {
            const circuit_layout_node_t *node = &layout->nodes[index];
            const int32_t left = node->x;
            const int32_t right = node->x + node->width;
            const int32_t top = node->y;
            const int32_t bottom = node->y + node->height;
            if (y1 == y2) {
                const int32_t low_x = minimum(x1, x2);
                const int32_t high_x = maximum(x1, x2);
                if (y1 > top && y1 < bottom && high_x > left && low_x < right) {
                    return false;
                }
            } else {
                const int32_t low_y = minimum(y1, y2);
                const int32_t high_y = maximum(y1, y2);
                if (x1 > left && x1 < right && high_y > top && low_y < bottom) {
                    return false;
                }
            }
        }
    }
    return true;
}
