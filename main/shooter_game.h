#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "joystick_input.h"
#include "key_input.h"
#include "lvgl.h"

typedef void (*shooter_game_complete_cb_t)(uint32_t stage_id, void *context);
typedef void (*shooter_game_exit_cb_t)(void *context);

lv_obj_t *shooter_game_create(uint32_t stage_id,
                              shooter_game_complete_cb_t complete_cb,
                              shooter_game_exit_cb_t exit_cb,
                              void *context);
lv_obj_t *shooter_game_stop(void);
void shooter_game_set_input(const key_input_state_t *keys,
                            const joystick_input_state_t *joystick);
bool shooter_game_is_active(void);
