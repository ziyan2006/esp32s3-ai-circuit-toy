#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

typedef enum {
    SSD1315_GATE_INPUT,
    SSD1315_GATE_OUTPUT,
    SSD1315_GATE_NOT,
    SSD1315_GATE_AND,
    SSD1315_GATE_OR,
    SSD1315_GATE_NAND,
    SSD1315_GATE_NOR,
    SSD1315_GATE_XOR,
    SSD1315_GATE_XNOR,
    SSD1315_GATE_NULL,
} ssd1315_gate_t;

esp_err_t ssd1315_oled_init(i2c_master_dev_handle_t device);
esp_err_t ssd1315_oled_show_gate(i2c_master_dev_handle_t device, ssd1315_gate_t gate);
esp_err_t ssd1315_oled_show_success(i2c_master_dev_handle_t device);
bool ssd1315_gate_from_eeprom_id(uint8_t id, ssd1315_gate_t *gate);
uint8_t ssd1315_gate_to_eeprom_id(ssd1315_gate_t gate);
ssd1315_gate_t ssd1315_gate_next(ssd1315_gate_t gate, int direction);
const char *ssd1315_gate_name(ssd1315_gate_t gate);
