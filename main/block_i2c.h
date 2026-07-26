#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "joystick_input.h"
#include "key_input.h"

esp_err_t block_i2c_init(void);
void block_i2c_submit_input(const key_input_state_t *keys,
                            const joystick_input_state_t *joystick);
bool block_i2c_programmer_present(void);
