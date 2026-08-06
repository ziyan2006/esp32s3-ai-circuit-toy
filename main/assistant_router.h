#pragma once

#include <stddef.h>
#include <stdint.h>

#include "assistant_diagnostics.h"
#include "esp_err.h"

esp_err_t assistant_router_init(void);
void assistant_router_begin_level_session(uint16_t level_id);
void assistant_router_end_level_session(void);
esp_err_t assistant_router_submit(const char *text, uint16_t level_id, uint32_t *out_request_id);
esp_err_t assistant_router_take_response(uint32_t request_id,
                                         assistant_response_t *out_response);
void assistant_router_cancel(uint32_t request_id);
void assistant_router_handle_mode_change(void);
esp_err_t assistant_router_self_test_run(void);
