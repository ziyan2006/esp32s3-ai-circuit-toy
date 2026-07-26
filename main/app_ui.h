#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "joystick_input.h"
#include "key_input.h"
#include "ssd1315_oled.h"

esp_err_t app_ui_init(void);
bool app_ui_gate_is_unlocked(ssd1315_gate_t gate);
esp_err_t app_ui_update(const key_input_state_t *keys,
                        const joystick_input_state_t *joystick,
                        uint32_t completed_ir_scans,
                        uint16_t ir_link_pairs,
                        bool programmer_owns_input);
