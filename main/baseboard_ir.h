#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board_mapping.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BASEBOARD_IR_PORT_COUNT BOARD_MAPPING_PORT_COUNT

typedef struct {
    uint64_t raw_rx[BASEBOARD_IR_PORT_COUNT];
    uint64_t logical_rx[BASEBOARD_IR_PORT_COUNT];
    uint32_t completed_scans;
    uint32_t slot_overruns;
    uint16_t link_pairs;
} baseboard_ir_matrix_t;

esp_err_t baseboard_ir_init(void);
void baseboard_ir_task(void *arg);
void baseboard_ir_set_input_task(TaskHandle_t task);
void baseboard_ir_set_ws2812_task(TaskHandle_t task);
uint8_t baseboard_ir_get_latest_input_byte(void);
void baseboard_ir_get_link_status(uint32_t *completed_scans, uint16_t *link_pairs);
bool baseboard_ir_get_matrix(baseboard_ir_matrix_t *matrix);
