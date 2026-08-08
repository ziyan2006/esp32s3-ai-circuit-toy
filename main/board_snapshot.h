#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "baseboard_ir.h"
#include "board_mapping.h"
#include "ssd1315_oled.h"

#define BOARD_SNAPSHOT_SLOT_COUNT       BOARD_MAPPING_SLOT_COUNT
#define BOARD_SNAPSHOT_PORTS_PER_SLOT   BOARD_MAPPING_PORTS_PER_SLOT
#define BOARD_SNAPSHOT_PORT_COUNT       BOARD_MAPPING_PORT_COUNT
#define BOARD_SNAPSHOT_MAX_LINKS        BOARD_SNAPSHOT_PORT_COUNT
#define BOARD_SNAPSHOT_PALETTE_SIZE     (BOARD_SNAPSHOT_PORT_COUNT / 2U)
#define BOARD_SNAPSHOT_INVALID_COLOR    UINT8_MAX
#define BOARD_ROLE_LABEL_MAX            8U

typedef enum {
    BOARD_PORT_UNUSED = 0,
    BOARD_PORT_INPUT,
    BOARD_PORT_OUTPUT,
} board_port_role_t;

typedef enum {
    BOARD_LINK_OK = 0,
    BOARD_LINK_UNUSED_PORT,
    BOARD_LINK_UNKNOWN_ENDPOINT,
    BOARD_LINK_DIRECTION_ERROR,
    BOARD_LINK_MULTIPLE_CONNECTIONS,
} board_link_error_t;

typedef struct {
    bool present;
    bool id_valid;
    uint8_t raw_id;
    ssd1315_gate_t gate;
    char role_label[BOARD_ROLE_LABEL_MAX];
} board_slot_identity_t;

typedef struct {
    uint8_t first_port;
    uint8_t second_port;
    uint8_t color_index;
    bool valid;
    board_link_error_t error;
} board_link_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} board_connection_color_t;

typedef struct {
    bool play_active;
    uint32_t generation;
    uint32_t topology_revision;
    uint32_t completed_ir_scans;
    uint32_t completed_i2c_scans;
    board_slot_identity_t slots[BOARD_SNAPSHOT_SLOT_COUNT];
    board_port_role_t port_roles[BOARD_SNAPSHOT_PORT_COUNT];
    board_link_t links[BOARD_SNAPSHOT_MAX_LINKS];
    uint8_t link_count;
    uint8_t ignored_link_count;
    uint8_t invalid_link_count;
    bool link_overflow;
} board_snapshot_t;

void board_snapshot_init(void);
void board_snapshot_reset(bool play_active);
void board_snapshot_publish_ir(const baseboard_ir_matrix_t *matrix);
void board_snapshot_publish_slots(const board_slot_identity_t *slots, uint32_t completed_scans);
bool board_snapshot_get(board_snapshot_t *snapshot);
board_connection_color_t board_snapshot_get_color(uint8_t color_index);
board_port_role_t board_snapshot_gate_port_role(ssd1315_gate_t gate,
                                                 uint8_t slot,
                                                 uint8_t local_port);

static inline bool board_link_is_ignored(const board_link_t *link)
{
    return link != NULL && link->error == BOARD_LINK_UNUSED_PORT;
}

static inline bool board_link_is_error(const board_link_t *link)
{
    return link != NULL && !link->valid && !board_link_is_ignored(link);
}
