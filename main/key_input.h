#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Physical button mapping on the baseboard, viewed from the user-facing side:
 * left button = Key3, center button = Key1, right button = Key0.
 */
typedef struct {
    bool key0_pressed;
    bool key1_pressed;
    bool key3_pressed;
    bool sw1_on;
    bool sw2_on;
    bool sw3_on;
    bool sw4_on;
} key_input_state_t;

esp_err_t key_input_init(void);
key_input_state_t key_input_update(uint8_t input_byte);
