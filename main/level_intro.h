#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    const char *speaker;
    const char *text;
} level_intro_page_t;

uint8_t level_intro_page_count(uint16_t level_id);
const level_intro_page_t *level_intro_page_get(uint16_t level_id, uint8_t page_index);
esp_err_t level_intro_self_test_run(void);
