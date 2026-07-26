#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "joystick_input.h"
#include "key_input.h"

esp_err_t lcd_status_init(void);
esp_err_t lcd_status_show_inputs(const key_input_state_t *key_state,
                                 const joystick_input_state_t *joystick_state,
                                 uint32_t completed_ir_scans,
                                 uint16_t link_pairs);
