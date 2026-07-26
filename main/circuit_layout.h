#pragma once

#include <stdint.h>

#include "board_snapshot.h"

#define CIRCUIT_LAYOUT_ROUTE_MAX_POINTS 6U

typedef struct {
    uint8_t slot;
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    int16_t port_x[BOARD_SNAPSHOT_PORTS_PER_SLOT];
    int16_t port_y[BOARD_SNAPSHOT_PORTS_PER_SLOT];
} circuit_layout_node_t;

typedef struct {
    uint8_t node_count;
    int8_t node_for_slot[BOARD_SNAPSHOT_SLOT_COUNT];
    circuit_layout_node_t nodes[BOARD_SNAPSHOT_SLOT_COUNT];
} circuit_layout_t;

typedef struct {
    uint8_t point_count;
    int16_t x[CIRCUIT_LAYOUT_ROUTE_MAX_POINTS];
    int16_t y[CIRCUIT_LAYOUT_ROUTE_MAX_POINTS];
} circuit_layout_route_t;

void circuit_layout_compute(const board_snapshot_t *snapshot,
                            int16_t width,
                            int16_t height,
                            circuit_layout_t *layout);
bool circuit_layout_route(const circuit_layout_t *layout,
                          int16_t width,
                          int16_t height,
                          uint8_t first_node_index,
                          uint8_t first_local_port,
                          uint8_t second_node_index,
                          uint8_t second_local_port,
                          uint8_t lane,
                          circuit_layout_route_t *route);
bool circuit_layout_route_avoids_nodes(const circuit_layout_t *layout,
                                       const circuit_layout_route_t *route);
