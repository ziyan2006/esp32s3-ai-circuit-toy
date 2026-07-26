#pragma once

#include "esp_err.h"

esp_err_t assistant_router_init(void);
void assistant_router_handle_mode_change(void);
esp_err_t assistant_router_self_test_run(void);
