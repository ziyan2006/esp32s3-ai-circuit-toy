#pragma once

#include <stdbool.h>

#include "esp_err.h"

/*
 * Corrected logical directions. The physical joystick is mounted with a
 * 90-degree clockwise rotation and mirror relative to these directions.
 */
typedef struct {
    bool up_pressed;
    bool down_pressed;
    bool left_pressed;
    bool right_pressed;
} joystick_input_state_t;

esp_err_t joystick_input_init(void);
joystick_input_state_t joystick_input_read(void);
