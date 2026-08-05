#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board_snapshot.h"

typedef enum {
    TUCO_PORT_HIGHLIGHT_CONNECT = 0,
    TUCO_PORT_HIGHLIGHT_DISCONNECT,
} tuco_port_highlight_intent_t;

void tuco_port_highlight_init(void);
void tuco_port_highlight_show(uint8_t output_port,
                              uint8_t input_port,
                              tuco_port_highlight_intent_t intent);
void tuco_port_highlight_show_slot(uint8_t slot);
void tuco_port_highlight_clear(void);
bool tuco_port_highlight_is_active(void);
bool tuco_port_highlight_get(const board_snapshot_t *snapshot,
                             uint8_t ports[BOARD_SNAPSHOT_PORTS_PER_SLOT],
                             uint8_t *port_count,
                             bool *visible,
                             tuco_port_highlight_intent_t *intent);
esp_err_t tuco_port_highlight_self_test_run(void);
