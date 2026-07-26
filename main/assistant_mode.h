#pragma once

#include "esp_err.h"

typedef enum {
    ASSISTANT_MODE_LOCAL = 0,
    ASSISTANT_MODE_REMOTE = 1,
} assistant_mode_t;

esp_err_t assistant_mode_init(void);
assistant_mode_t assistant_mode_get(void);
esp_err_t assistant_mode_set(assistant_mode_t mode);
esp_err_t assistant_mode_self_test_run(void);
