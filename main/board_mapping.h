#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define BOARD_MAPPING_SLOT_COUNT       16U
#define BOARD_MAPPING_PORTS_PER_SLOT   4U
#define BOARD_MAPPING_PORT_COUNT       (BOARD_MAPPING_SLOT_COUNT * BOARD_MAPPING_PORTS_PER_SLOT)

typedef enum {
    BOARD_PORT_SIDE_UP = 0,
    BOARD_PORT_SIDE_DOWN,
    BOARD_PORT_SIDE_LEFT,
    BOARD_PORT_SIDE_RIGHT,
} board_port_side_t;

typedef struct {
    uint8_t module_number;
    uint8_t module_slot;
    uint8_t grid_row;
    uint8_t grid_column;
    uint8_t tca_address;
    uint8_t tca_channel;
} board_slot_mapping_t;

const board_slot_mapping_t *board_mapping_slot(uint8_t slot);
uint8_t board_mapping_slot_for_port(uint8_t port);
uint8_t board_mapping_local_port(uint8_t port);
board_port_side_t board_mapping_port_side(uint8_t slot, uint8_t local_port);
uint8_t board_mapping_ir_rx_raw_bit_to_port(uint8_t raw_bit);
uint8_t board_mapping_ws2812_index_for_port(uint8_t port);
const char *board_mapping_side_name(board_port_side_t side);
esp_err_t board_mapping_validate(void);
