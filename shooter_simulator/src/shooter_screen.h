#ifndef SHOOTER_SCREEN_H
#define SHOOTER_SCREEN_H

#include "lvgl.h"
#include "shooter_core.h"

lv_obj_t *shooter_screen_create(lv_display_t *display, uint32_t level_id);
void shooter_screen_exit_to_home(void);
void shooter_screen_set_input(const shooter_input_state_t *input);
void shooter_screen_force_start_level(uint32_t level);

#endif
