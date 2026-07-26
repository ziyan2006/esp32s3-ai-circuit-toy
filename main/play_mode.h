#pragma once

#include <stdbool.h>
#include <stdint.h>

void play_mode_set_active(bool active);
bool play_mode_is_active(void);
uint32_t play_mode_generation(void);
